<#
.SYNOPSIS
    cds_95.exe 라이브 메모리 범용 스냅샷/차등 도구. 값을 모르는 상태 변수
    (화면/메뉴 상태 등)를 "화면 전환 시 바뀌는 바이트"로 좁혀 찾는 데 쓴다.

.DESCRIPTION
    모드:
      capture   : writable 메모리 전체를 슬롯 파일로 저장.       -Slot <이름>
      changed   : 슬롯과 현재 메모리를 비교, 바뀐 주소 목록 생성.  -From <슬롯> [-Out <파일>] [-Intersect <파일>]
      inspect   : 후보 목록(또는 주소들)의 현재 값을 출력.         -Addrs <파일|콤마주소> [-Max N]

    화면 상태 변수 찾는 표준 절차 (enter/leave 교집합):
      1) A화면에서:  capture -Slot A
      2) B화면으로 이동 후:  changed -From A -Out enter.bin      (A→B 때 바뀐 것)
      3) B화면에서:  capture -Slot B
      4) A화면으로 복귀 후:  changed -From B -Intersect enter.bin -Out both.bin
         → enter.bin(들어갈 때 변함) ∩ leave(나올 때 변함) = 그 화면과 함께 토글되는 후보
      5) inspect -Addrs both.bin  로 각 화면에서 값 확인 → 상태 변수 확정
#>
param(
    [Parameter(Mandatory=$true)][ValidateSet('capture','changed','inspect')][string]$Mode,
    [string]$Slot = "A",
    [string]$From = "A",
    [string]$Out = "$env:TEMP\cds_changed.bin",
    [string]$Intersect = "",
    [string]$Addrs = "",
    [int]$Max = 60,
    [string]$ProcessName = "cds_95"
)
$ErrorActionPreference = "Stop"
function SlotPath($name){ Join-Path $env:TEMP ("cds_snap_{0}.bin" -f $name) }

if (-not ("CdsSnap.Mem" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
namespace CdsSnap { public static class Mem {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, int p);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] b, int s, out int r);
  [DllImport("kernel32.dll")] public static extern int VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, int l);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct MBI {
    public IntPtr BaseAddress, AllocationBase; public uint AllocationProtect;
    public IntPtr RegionSize; public uint State, Protect, Type; }
}}
"@
}
$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Host "'$ProcessName' 프로세스 없음." -ForegroundColor Yellow; exit 1 }
$H = [CdsSnap.Mem]::OpenProcess(0x0410, $false, $proc.Id)
if ($H -eq [IntPtr]::Zero) { throw "OpenProcess 실패" }

function RB([long]$a,[int]$n){ $b=New-Object byte[] $n; $r=0; $ok=[CdsSnap.Mem]::ReadProcessMemory($H,[IntPtr]$a,$b,$n,[ref]$r); if($ok -and $r -gt 0){ if($r -ne $n){ $t=New-Object byte[] $r; [Array]::Copy($b,$t,$r); return ,$t }; return ,$b }; return $null }

function Get-Regions {
    $sz=[Runtime.InteropServices.Marshal]::SizeOf([type][CdsSnap.Mem+MBI])
    $cur=0L; $rs=@(); $mbi=New-Object CdsSnap.Mem+MBI
    while([CdsSnap.Mem]::VirtualQueryEx($H,[IntPtr]$cur,[ref]$mbi,$sz) -ne 0){
        $base=$mbi.BaseAddress.ToInt64(); $len=$mbi.RegionSize.ToInt64()
        if($len -le 0){break}
        if($mbi.State -eq 0x1000 -and (($mbi.Protect -band 0xEE) -ne 0) -and $base -lt 0x10000000 -and $len -lt 64MB){
            $buf=RB $base $len; if($buf){ $rs+=[PSCustomObject]@{Base=$base;Buf=$buf} }
        }
        $next=$base+$len; if($next -le $cur){break}; $cur=$next
    }
    return $rs
}
function Save-Snapshot($regions,$path){
    $fs=[System.IO.File]::Create($path); $bw=New-Object System.IO.BinaryWriter($fs)
    foreach($rg in $regions){ $bw.Write([long]$rg.Base); $bw.Write([int]$rg.Buf.Length); $bw.Write($rg.Buf) }
    $bw.Flush();$bw.Close();$fs.Close()
}
function Load-Snapshot($path){
    $map=@{}; $fs=[System.IO.File]::OpenRead($path); $br=New-Object System.IO.BinaryReader($fs)
    while($fs.Position -lt $fs.Length){ $base=$br.ReadInt64(); $len=$br.ReadInt32(); $map[$base]=$br.ReadBytes($len) }
    $br.Close();$fs.Close(); return $map
}
function Save-Addrs($set,$path){
    $fs=[System.IO.File]::Create($path); $bw=New-Object System.IO.BinaryWriter($fs)
    foreach($a in $set){ $bw.Write([long]$a) }; $bw.Flush();$bw.Close();$fs.Close()
}
function Load-Addrs($path){
    $set=New-Object 'System.Collections.Generic.HashSet[long]'
    $fs=[System.IO.File]::OpenRead($path); $br=New-Object System.IO.BinaryReader($fs)
    while($fs.Position -lt $fs.Length){ [void]$set.Add($br.ReadInt64()) }
    $br.Close();$fs.Close(); return $set
}

try {
  if ($Mode -eq 'capture') {
      $r = Get-Regions
      Save-Snapshot $r (SlotPath $Slot)
      $tot = 0L; foreach($x in $r){ $tot += $x.Buf.Length }
      Write-Host ("capture 슬롯 '{0}' 저장: 영역 {1}개, {2:N0} bytes" -f $Slot,$r.Count,$tot) -ForegroundColor Green
  }
  elseif ($Mode -eq 'changed') {
      $slotPath = SlotPath $From
      if (-not (Test-Path $slotPath)) { throw "슬롯 '$From' 없음. 먼저 capture -Slot $From." }
      $old = Load-Snapshot $slotPath
      $cur = Get-Regions
      $changed = New-Object 'System.Collections.Generic.HashSet[long]'
      foreach($rg in $cur){
          if(-not $old.ContainsKey($rg.Base)){ continue }
          $ob=$old[$rg.Base]; $nb=$rg.Buf; $len=[Math]::Min($ob.Length,$nb.Length)
          for($i=0;$i -lt $len;$i++){ if($ob[$i] -ne $nb[$i]){ [void]$changed.Add($rg.Base+$i) } }
      }
      Write-Host ("바뀐 주소: {0}개" -f $changed.Count) -ForegroundColor Cyan
      if ($Intersect -ne "" -and (Test-Path $Intersect)) {
          $prev = Load-Addrs $Intersect
          $keep = New-Object 'System.Collections.Generic.HashSet[long]'
          foreach($a in $changed){ if($prev.Contains($a)){ [void]$keep.Add($a) } }
          $changed = $keep
          Write-Host ("∩ '{0}' 교집합 후: {1}개" -f (Split-Path $Intersect -Leaf),$changed.Count) -ForegroundColor Cyan
      }
      Save-Addrs $changed $Out
      Write-Host ("→ 저장: $Out" ) -ForegroundColor DarkGray
      if ($changed.Count -le $Max) {
          foreach($a in ($changed | Sort-Object)){
              $b=RB $a 4
              if($b){ Write-Host ("  0x{0:X8}  byte={1,3}  i32={2}" -f $a,$b[0],[BitConverter]::ToInt32($b,0)) }
          }
      } else { Write-Host ("  (너무 많음 — 다른 화면 전환으로 -Intersect 교집합 내서 좁히세요)") -ForegroundColor Yellow }
  }
  elseif ($Mode -eq 'inspect') {
      if ($Addrs -eq "") { throw "-Addrs <파일|콤마주소> 필요" }
      $list=@()
      if (Test-Path $Addrs) { $list = (Load-Addrs $Addrs) | Sort-Object }
      else { $list = $Addrs.Split(',') | ForEach-Object { [Convert]::ToInt64($_.Trim().TrimStart('0','x','X'),16) } }
      Write-Host ("inspect {0}개:" -f @($list).Count) -ForegroundColor Cyan
      foreach($a in ($list | Select-Object -First $Max)){
          $b=RB $a 4
          if($b){ Write-Host ("  0x{0:X8}  byte={1,3}  i16={2,6}  i32={3}" -f $a,$b[0],[BitConverter]::ToInt16($b,0),[BitConverter]::ToInt32($b,0)) }
          else  { Write-Host ("  0x{0:X8}  read-fail" -f $a) }
      }
  }
}
finally { [void][CdsSnap.Mem]::CloseHandle($H) }
