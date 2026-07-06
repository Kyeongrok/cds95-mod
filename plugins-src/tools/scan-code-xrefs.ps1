<#
.SYNOPSIS
    cds_95.exe(또는 임의 PE) 안에서 주어진 절대주소(VA)를 참조하는 코드 위치를 정적으로
    찾는다. 라이브 스캔으로 찾은 데이터 주소(날짜/소지금 등)를 건드리는 "루틴"을 역추적해
    MinHook 대상 함수를 좁히는 용도.

.DESCRIPTION
    동작:
      - PE 섹션 테이블을 파싱해 파일오프셋 <-> VA 매핑.
      - -Targets 로 준 각 VA(4바이트 LE)를 파일 전체에서 바이트 검색.
      - 각 히트를 VA로 환산해 출력 (어느 섹션인지, 앞뒤 컨텍스트 바이트 포함).
      - -Near30 지정 시, 각 날짜 히트 주변 ±Window 에서 상수 30(0x1E) 등장 위치도 표시
        (숙박이 "30일" 넘기는 지점의 앵커).

.EXAMPLE
    .\scan-code-xrefs.ps1 -ExePath "$env:USERPROFILE\Desktop\대항해시대3\cds_95.exe" `
        -Targets 0x005A4D20,0x005A4D24,0x005A4D28,0x005B6194 -Near30
#>
param(
    [string]$ExePath = "$env:USERPROFILE\Desktop\대항해시대3\cds_95.exe",
    [Parameter(Mandatory=$true)][long[]]$Targets,
    [switch]$Near30,
    [int]$Window = 0x100
)
$ErrorActionPreference="Stop"
if(-not (Test-Path $ExePath)){ throw "exe 없음: $ExePath" }
$bytes=[System.IO.File]::ReadAllBytes($ExePath)

$e_lfanew=[BitConverter]::ToInt32($bytes,0x3C)
$fh=$e_lfanew+4
$numSec=[BitConverter]::ToUInt16($bytes,$fh+2)
$optSize=[BitConverter]::ToUInt16($bytes,$fh+16)
$opt=$fh+20
$imageBase=[BitConverter]::ToUInt32($bytes,$opt+28)
$secTab=$opt+$optSize
$sections=@()
for($s=0;$s -lt $numSec;$s++){
    $off=$secTab+($s*40)
    $name=[System.Text.Encoding]::ASCII.GetString($bytes,$off,8).TrimEnd([char]0)
    $vsize=[BitConverter]::ToUInt32($bytes,$off+8)
    $va=[BitConverter]::ToUInt32($bytes,$off+12)
    $rsize=[BitConverter]::ToUInt32($bytes,$off+16)
    $rptr=[BitConverter]::ToUInt32($bytes,$off+20)
    $sections+=[PSCustomObject]@{Name=$name;VA=$va;VSize=$vsize;RawPtr=$rptr;RawSize=$rsize}
}
Write-Host ("imageBase=0x{0:X}  sections:" -f $imageBase) -ForegroundColor DarkGray
foreach($s in $sections){ Write-Host ("  {0,-8} VA=0x{1:X8} VSize=0x{2:X} RawPtr=0x{3:X} RawSize=0x{4:X}" -f $s.Name,($imageBase+$s.VA),$s.VSize,$s.RawPtr,$s.RawSize) }

function Off2VA($off){
    foreach($s in $sections){
        if($off -ge $s.RawPtr -and $off -lt ($s.RawPtr+$s.RawSize)){
            return @{ VA=($imageBase + $s.VA + ($off - $s.RawPtr)); Sec=$s.Name }
        }
    }
    return $null
}
function VA2Off($va){
    $rva=$va-$imageBase
    foreach($s in $sections){
        if($rva -ge $s.VA -and $rva -lt ($s.VA+$s.VSize)){
            $o=$s.RawPtr+($rva-$s.VA)
            if($o -lt ($s.RawPtr+$s.RawSize)){ return $o }
        }
    }
    return $null
}

foreach($t in $Targets){
    $pat=[BitConverter]::GetBytes([uint32]$t)
    Write-Host ("`n=== VA 0x{0:X8} 참조 검색 (LE {1:X2} {2:X2} {3:X2} {4:X2}) ===" -f $t,$pat[0],$pat[1],$pat[2],$pat[3]) -ForegroundColor Cyan
    $found=0
    for($i=0;$i -le $bytes.Length-4;$i++){
        if($bytes[$i] -eq $pat[0] -and $bytes[$i+1] -eq $pat[1] -and $bytes[$i+2] -eq $pat[2] -and $bytes[$i+3] -eq $pat[3]){
            $m=Off2VA $i
            if($null -eq $m){ continue }
            $cs=[Math]::Max(0,$i-6); $ce=[Math]::Min($bytes.Length-1,$i+9)
            $ctx=($bytes[$cs..$ce] | ForEach-Object { $_.ToString('x2') }) -join ' '
            Write-Host ("  refVA=0x{0:X8} [{1}]  fileOff=0x{2:X}  ctx: {3}" -f $m.VA,$m.Sec,$i,$ctx)
            $found++
            if($Near30){
                $ws=[Math]::Max(0,$i-$Window); $we=[Math]::Min($bytes.Length-1,$i+$Window)
                $th=@()
                for($j=$ws;$j -le $we;$j++){ if($bytes[$j] -eq 0x1E){ $mj=Off2VA $j; if($mj){ $th+=("0x{0:X8}" -f $mj.VA) } } }
                if($th.Count -gt 0){ Write-Host ("      ↳ 근처 0x1E(30): {0}" -f ($th -join ', ')) -ForegroundColor DarkYellow }
            }
        }
    }
    if($found -eq 0){ Write-Host "  (참조 없음)" -ForegroundColor DarkGray }
}
