<#
.SYNOPSIS
    cds_95.exe 라이브 메모리에서 "값이 특정 범위[lo,hi]에 모여 일정 간격으로
    길게 반복되는 배열"을 찾는다. (예: 도시별 시세 90~105가 도시 수만큼 stride 간격 반복)

.DESCRIPTION
    단일 값은 노이즈지만, [lo,hi] 값이 stride 간격으로 ~226회(도시 수) 늘어선 구조는
    거의 유일하다 → 그 (base, stride, width) 가 도시 구조체 배열.

    각 width(1/2/4바이트, little-endian)에 대해 in-range 위치를 표시한 뒤,
    stride 후보를 스윕하며 "base+k*stride 가 연속 in-range" 인 최장 런을 찾는다.
    (허용 gap 있음 — 몇몇 도시가 범위 밖이어도 런이 끊기지 않게.)

.EXAMPLE
    # 기본: 라이브 상태값 근방(0x00560000~0x005D0000)에서 90~105 클러스터 탐색
    .\find-array-cluster.ps1

    # 전체 메모리, 범위/기대개수 조정
    .\find-array-cluster.ps1 -RangeStart 0 -RangeEnd 0x10000000 -Lo 80 -Hi 120 -Count 226
#>
param(
    [int]$Lo = 88,
    [int]$Hi = 108,
    [int]$Count = 226,          # 기대 도시 수(런 길이 상한 표시에만 사용)
    [int]$MinRun = 30,          # 이 길이 이상 런만 보고
    [int]$MaxStride = 512,
    [int]$Gaps = 4,             # 런 도중 허용하는 out-of-range 슬롯 수
    [long]$RangeStart = 0x00560000,
    [long]$RangeEnd   = 0x005D0000,
    [int[]]$Widths = @(1,2,4),
    [int]$Top = 20,
    [string]$ProcessName = "cds_95"
)
$ErrorActionPreference = "Stop"

if (-not ("CdsCluster.Mem" -as [type])) {
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
namespace CdsCluster {
 public struct Hit { public long Addr; public int Stride; public int Width; public int Run; public int InWindow; }
 public static class Mem {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, int p);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] b, int s, out int r);
  [DllImport("kernel32.dll")] public static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int l);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct MBI {
    public IntPtr BaseAddress, AllocationBase; public uint AllocationProtect;
    public IntPtr RegionSize; public uint State, Protect, Type; }

  static long ReadW(byte[] b, int i, int w){
    if (w==1) return b[i];
    if (w==2) return (long)(b[i] | (b[i+1]<<8));
    return (long)(uint)(b[i] | (b[i+1]<<8) | (b[i+2]<<16) | (b[i+3]<<24));
  }

  // 한 region 버퍼(base..base+len) 안에서 클러스터 배열 런을 찾는다.
  public static List<Hit> Scan(byte[] buf, long baseAddr, int lo, int hi, int width,
                               int maxStride, int gaps, int minRun, int count){
    int len = buf.Length;
    var hits = new List<Hit>();
    if (len < width) return hits;
    bool[] hit = new bool[len];
    for (int i=0; i+width<=len; i++){ long v=ReadW(buf,i,width); hit[i] = (v>=lo && v<=hi); }
    for (int s=width; s<=maxStride; s++){
      for (int start=0; start+width<=len; start++){
        if (!hit[start]) continue;
        if (start-s>=0 && hit[start-s]) continue;   // 런 시작점만(중복 방지)
        int run=1, g=0, k=1, best=1, inWin=1;
        for (;;){
          long pos = (long)start + (long)k*s;
          if (pos+width>len) break;
          if (k < count && ReadW(buf,(int)pos,width)>=lo && ReadW(buf,(int)pos,width)<=hi) inWin++;
          if (hit[(int)pos]){ run++; if(run>best) best=run; }
          else { g++; if(g>gaps) break; }
          k++;
          if (k > count+gaps) break;
        }
        if (best>=minRun){
          Hit hh; hh.Addr=baseAddr+start; hh.Stride=s; hh.Width=width; hh.Run=best; hh.InWindow=inWin;
          hits.Add(hh);
        }
      }
    }
    return hits;
  }
 }
}
"@
}

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Host "'$ProcessName' 프로세스 없음. 게임을 켜세요." -ForegroundColor Yellow; exit 1 }
$hProc = [CdsCluster.Mem]::OpenProcess(0x0410, $false, $proc.Id)
if ($hProc -eq [IntPtr]::Zero) { throw "OpenProcess 실패 (관리자 권한으로 실행해 보세요)" }

function RB([long]$a,[int]$n){ $b=New-Object byte[] $n; $r=0; $ok=[CdsCluster.Mem]::ReadProcessMemory($hProc,[IntPtr]$a,$b,$n,[ref]$r); if($ok -and $r -eq $n){ return ,$b }; return $null }

try {
  # 대상 region 수집 (쓰기가능 + [RangeStart,RangeEnd] 교차)
  $sz=[Runtime.InteropServices.Marshal]::SizeOf([type][CdsCluster.Mem+MBI])
  $cur=$RangeStart; $mbi=New-Object CdsCluster.Mem+MBI
  $all = New-Object System.Collections.Generic.List[object]
  while([CdsCluster.Mem]::VirtualQueryEx($hProc,[IntPtr]$cur,[ref]$mbi,$sz) -ne 0){
      $base=$mbi.BaseAddress.ToInt64(); $len=$mbi.RegionSize.ToInt64()
      if($len -le 0){break}
      $end=$base+$len
      if($base -ge $RangeEnd){break}
      if($mbi.State -eq 0x1000 -and (($mbi.Protect -band 0xEE) -ne 0) -and $len -lt 64MB){
          # 스캔 창으로 클리핑
          $rs=[Math]::Max($base,$RangeStart); $re=[Math]::Min($end,$RangeEnd)
          if($re -gt $rs){
              $buf=RB $rs ([int]($re-$rs))
              if($buf){ $all.Add([PSCustomObject]@{Base=$rs;Buf=$buf}) }
          }
      }
      $next=$end; if($next -le $cur){break}; $cur=$next
  }
  $totBytes=0L; foreach($rg in $all){ $totBytes+=$rg.Buf.Length }
  Write-Host ("스캔 대상: region {0}개, {1:N0} bytes  범위=[0x{2:X}..0x{3:X})  값[{4}..{5}] width={6}" -f `
      $all.Count,$totBytes,$RangeStart,$RangeEnd,$Lo,$Hi,($Widths -join ',')) -ForegroundColor Cyan

  $results = New-Object System.Collections.Generic.List[object]
  foreach($rg in $all){
      foreach($w in $Widths){
          $hits = [CdsCluster.Mem]::Scan($rg.Buf, $rg.Base, $Lo, $Hi, $w, $MaxStride, $Gaps, $MinRun, $Count)
          foreach($one in $hits){ $results.Add($one) }
      }
  }
  Write-Host ("후보 런: {0}개 (MinRun>={1})" -f $results.Count,$MinRun) -ForegroundColor Cyan
  if($results.Count -eq 0){
      Write-Host "  없음. -Lo/-Hi 범위를 넓히거나 -RangeStart/-RangeEnd 를 확대(-RangeEnd 0x10000000)해 보세요." -ForegroundColor Yellow
  }

  # 런 길이 우선, 그다음 InWindow 로 정렬해 상위 N 출력 (+ 실제 값 미리보기)
  $sorted = $results | Sort-Object -Property @{E={$_.Run};Descending=$true}, @{E={$_.InWindow};Descending=$true}
  $shown=0
  foreach($cand in $sorted){
      if($shown -ge $Top){break}; $shown++
      Write-Host ("`n[{0}] base=0x{1:X8} stride={2} width={3}  run={4}  in-{5}window={6}/{7}" -f `
          $shown,$cand.Addr,$cand.Stride,$cand.Width,$cand.Run,$Count,$cand.InWindow,$Count) -ForegroundColor Green
      # 앞쪽 24개 슬롯 값 미리보기
      $line="  vals:"; $b=RB $cand.Addr ([int]([Math]::Min(([long]$cand.Stride*24+$cand.Width),4096)))
      if($b){
          for($k=0;$k -lt 24;$k++){
              $off=$k*$cand.Stride
              if($off+$cand.Width -gt $b.Length){break}
              $v = if($cand.Width -eq 1){$b[$off]} elseif($cand.Width -eq 2){[BitConverter]::ToUInt16($b,$off)} else {[BitConverter]::ToInt32($b,$off)}
              $line += (" {0}" -f $v)
          }
          Write-Host $line -ForegroundColor Gray
      }
  }
  Write-Host "`n다음: 위 후보 중 stride가 도시 구조체 크기답고, vals가 화면 시세와 맞는 것을 고르세요." -ForegroundColor DarkGray
  Write-Host "현재 입항 도시 ID = [0x005B6154](byte). 그 슬롯 값이 화면 시세와 같으면 확정." -ForegroundColor DarkGray
}
finally { [void][CdsCluster.Mem]::CloseHandle($hProc) }
