<#
.SYNOPSIS
    차등(differential) 메모리 스캔으로 한국어판 라이브 년/월/일 주소를 찾는다.
    정적 가설(cds-helper 절대주소)이 이 빌드에서 안 맞을 때 사용.

.DESCRIPTION
    2단계로 동작:
      capture : 현재 게임 날짜(-Day 값)를 가진 모든 바이트 주소를 스냅샷 파일에 저장.
      diff    : 게임에서 하루 진행시킨 뒤(-Day 가 +1 된 값), capture 때 저장한 후보 중
                실제로 그 값으로 바뀐 주소만 남기고, 근처(±Prox)에 년(u16)·월(u8)이
                있는지 교차검증해서 날짜 구조체 위치를 특정한다.

    쓰는 법:
      1) 게임에서 날짜 확인 (예: 1480/6/19)
      2) .\find-date-by-diff.ps1 -Mode capture -Year 1480 -Month 6 -Day 19
      3) 게임에서 딱 하루 진행 (여관 1일 숙박 등) → 1480/6/20
      4) .\find-date-by-diff.ps1 -Mode diff -Year 1480 -Month 6 -Day 20
#>
param(
    [Parameter(Mandatory=$true)][ValidateSet('capture','diff')][string]$Mode,
    [Parameter(Mandatory=$true)][int]$Year,
    [Parameter(Mandatory=$true)][int]$Month,
    [Parameter(Mandatory=$true)][int]$Day,
    [string]$SnapFile = "$env:TEMP\cds_date_daycandidates.bin",
    [int]$Prox = 64,
    [string]$ProcessName = "cds_95"
)
$ErrorActionPreference = "Stop"

if (-not ("CdsDiff.Mem" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
namespace CdsDiff {
  public static class Mem {
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, int p);
    [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] b, int s, out int r);
    [DllImport("kernel32.dll")] public static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int l);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct MBI {
      public IntPtr BaseAddress, AllocationBase; public uint AllocationProtect;
      public IntPtr RegionSize; public uint State, Protect, Type; }
  }
}
"@
}

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Host "'$ProcessName' 프로세스 없음. 게임 실행/로드 확인." -ForegroundColor Yellow; exit 1 }
$h = [CdsDiff.Mem]::OpenProcess(0x0410, $false, $proc.Id)
if ($h -eq [IntPtr]::Zero) { throw "OpenProcess 실패" }

function Read-Region([long]$a,[int]$n){ $b=New-Object byte[] $n; $r=0; $ok=[CdsDiff.Mem]::ReadProcessMemory($h,[IntPtr]$a,$b,$n,[ref]$r); if($ok -and $r -gt 0){ if($r -ne $n){ $t=New-Object byte[] $r; [Array]::Copy($b,$t,$r); return ,$t }; return ,$b }; return $null }

function Get-Regions {
    $mbiSize=[Runtime.InteropServices.Marshal]::SizeOf([type][CdsDiff.Mem+MBI])
    $cur=0L; $regions=@(); $mbi=New-Object CdsDiff.Mem+MBI
    while([CdsDiff.Mem]::VirtualQueryEx($h,[IntPtr]$cur,[ref]$mbi,$mbiSize) -ne 0){
        $base=$mbi.BaseAddress.ToInt64(); $rs=$mbi.RegionSize.ToInt64()
        if($rs -le 0){break}
        if($mbi.State -eq 0x1000 -and (($mbi.Protect -band 0xEE) -ne 0) -and $base -lt 0x10000000 -and $rs -lt 64MB){
            $buf=Read-Region $base $rs; if($buf){ $regions+=[PSCustomObject]@{Base=$base;Buf=$buf} }
        }
        $next=$base+$rs; if($next -le $cur){break}; $cur=$next
    }
    return $regions
}

try {
  $regions = Get-Regions
  Write-Host ("regions {0}, date {1}/{2}/{3}" -f $regions.Count,$Year,$Month,$Day) -ForegroundColor DarkGray

  if ($Mode -eq 'capture') {
      # 현재 Day 값을 가진 모든 주소 저장 (int64 배열, binary)
      $fs = [System.IO.File]::Create($SnapFile)
      $bw = New-Object System.IO.BinaryWriter($fs)
      $count = 0
      foreach($rg in $regions){ $buf=$rg.Buf; $base=$rg.Base
          for($i=0;$i -lt $buf.Length;$i++){ if($buf[$i] -eq $Day){ $bw.Write([long]($base+$i)); $count++ } }
      }
      $bw.Flush(); $bw.Close(); $fs.Close()
      Write-Host "capture 완료: Day=$Day 후보 $count 개 → $SnapFile" -ForegroundColor Green
      Write-Host "이제 게임에서 딱 하루 진행시킨 뒤(Day=$($Day+1)), diff 모드로 다시 실행하세요." -ForegroundColor Cyan
  }
  else {
      if (-not (Test-Path $SnapFile)) { throw "스냅샷 파일 없음: $SnapFile (먼저 capture 실행)" }
      # region 조회 헬퍼: 주소 → 현재 바이트
      $sorted = $regions | Sort-Object Base
      $bases = $sorted.Base
      function ByteAt([long]$addr){
          $idx=[Array]::BinarySearch([long[]]$bases,$addr)
          if($idx -lt 0){ $idx = (-$idx) - 2 }
          if($idx -lt 0){ return $null }
          $rg=$sorted[$idx]; $off=$addr-$rg.Base
          if($off -ge 0 -and $off -lt $rg.Buf.Length){ return $rg.Buf[$off] }
          return $null
      }
      function U16At([long]$addr){
          $b0=ByteAt $addr; $b1=ByteAt ($addr+1)
          if($null -eq $b0 -or $null -eq $b1){ return $null }
          return $b0 -bor ($b1 -shl 8)
      }
      # capture된 후보 로드
      $fs=[System.IO.File]::OpenRead($SnapFile); $br=New-Object System.IO.BinaryReader($fs)
      $cand=New-Object System.Collections.Generic.List[long]
      while($fs.Position -lt $fs.Length){ $cand.Add($br.ReadInt64()) }
      $br.Close(); $fs.Close()
      Write-Host ("capture 후보 로드: $($cand.Count) 개") -ForegroundColor DarkGray

      # 1) 값이 Day(=이전+1)로 바뀐 주소만
      $dayHits=@()
      foreach($a in $cand){ $v=ByteAt $a; if($null -ne $v -and $v -eq $Day){ $dayHits+=$a } }
      Write-Host ("Day 값으로 바뀐 후보: $($dayHits.Count) 개") -ForegroundColor Cyan

      # 2) 근처에 년(u16)과 월(u8)이 있는지 교차검증
      $final=@()
      foreach($a in $dayHits){
          $yearNear=$null; $monthNear=$false
          for($d=-$Prox; $d -le $Prox; $d++){
              if((U16At ($a+$d)) -eq $Year){ if($null -eq $yearNear){ $yearNear=$a+$d } }
              if((ByteAt ($a+$d)) -eq $Month){ $monthNear=$true }
          }
          if($null -ne $yearNear){
              $final+=[PSCustomObject]@{ DayAddr=$a; YearAddr=$yearNear; MonthNear=$monthNear }
          }
      }
      Write-Host ("`n=== 최종 후보 (근처에 년값 있는 것) : $($final.Count) 개 ===") -ForegroundColor Green
      foreach($f in $final){
          Write-Host ("  DayAddr=0x{0:X8}  YearAddr=0x{1:X8}  Δ(day-year)={2}  month근처={3}" -f `
              $f.DayAddr,$f.YearAddr,($f.DayAddr-$f.YearAddr),$f.MonthNear)
      }
      if($final.Count -eq 0){
          Write-Host "  근처 년값 없는 순수 Day 후보 (참고, 상위 40개):" -ForegroundColor Yellow
          foreach($a in ($dayHits | Select-Object -First 40)){ Write-Host ("    0x{0:X8}" -f $a) }
      }
  }
}
finally { [void][CdsDiff.Mem]::CloseHandle($h) }
