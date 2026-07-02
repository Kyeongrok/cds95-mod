<#
.SYNOPSIS
    CDS95Util *.plugin 바이너리에서 버전 분기(CMP EAX, imm32)와 그에 딸린
    하드코딩된 게임 주소(MOV [addr],imm32 / MOV reg,[addr])를 정적으로 찾아냅니다.

.DESCRIPTION
    plugins-src/existing-plugin-addresses.md 에 정리된 분석에 쓰인 스캔 로직입니다.
    Cheat Engine 등 동적 분석 없이, 이미 갖고 있는 .plugin 바이너리만으로
    "이 플러그인이 어떤 버전에서 어떤 주소를 건드리는지"를 추출합니다.

    발견 원리:
      - 이 프로젝트의 플러그인들은 버전을 4바이트로 팩(major,minor,build,revision)해서
        `CMP EAX, imm32` 한 방으로 비교합니다. imm32를 바이트로 쪼개면 버전 그 자체입니다.
      - 그 분기 안에서 게임 주소가 다음 두 형태로 나타납니다.
          C7 05 <addr32> <imm32>   : MOV dword ptr [addr32], imm32  (imm32가 게임 주소)
          8B 3D/35/0D <addr32>     : MOV EDI/ESI/ECX, [addr32]      (addr32 자체가 게임 주소)

.PARAMETER PluginPath
    분석할 .plugin(또는 임의의 PE32 DLL/EXE) 파일 경로.

.EXAMPLE
    .\scan-hardcoded-addresses.ps1 -PluginPath "..\..\CDS95Util\CollectionUtil.plugin"
#>
param(
    [Parameter(Mandatory = $true)]
    [string]$PluginPath
)

$ErrorActionPreference = "Stop"

function Get-Sections($path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    $e_lfanew = [BitConverter]::ToInt32($bytes, 0x3C)
    $fileHeaderOffset = $e_lfanew + 4
    $numberOfSections = [BitConverter]::ToUInt16($bytes, $fileHeaderOffset + 2)
    $sizeOfOptionalHeader = [BitConverter]::ToUInt16($bytes, $fileHeaderOffset + 16)
    $sectionTableOffset = $fileHeaderOffset + 20 + $sizeOfOptionalHeader
    $sections = @()
    for ($s = 0; $s -lt $numberOfSections; $s++) {
        $off = $sectionTableOffset + ($s * 40)
        $name = [System.Text.Encoding]::ASCII.GetString($bytes, $off, 8).TrimEnd([char]0)
        $rawSize = [BitConverter]::ToUInt32($bytes, $off + 16)
        $rawPtr = [BitConverter]::ToUInt32($bytes, $off + 20)
        $sections += [PSCustomObject]@{ Name = $name; RawPtr = $rawPtr; RawSize = $rawSize }
    }
    return $sections
}

# CMP EAX, imm32 (3D id id id id) 에서 imm32가 "그럴듯한 버전 팩"인지 판별
function Find-VersionCompares($bytes, $rawPtr, $rawSize) {
    $end = $rawPtr + $rawSize
    $results = @()
    for ($i = $rawPtr; $i -lt $end - 5; $i++) {
        if ($bytes[$i] -eq 0x3D) {
            $b0 = $bytes[$i + 1]; $b1 = $bytes[$i + 2]; $b2 = $bytes[$i + 3]; $b3 = $bytes[$i + 4]
            if ($b3 -ge 1 -and $b3 -le 2 -and $b2 -le 9 -and $b1 -le 99 -and $b0 -le 9) {
                $results += [PSCustomObject]@{
                    FileOffset     = $i
                    GuessedVersion = "$b3.$b2.$b1.$b0"
                }
            }
        }
    }
    return $results
}

# 버전 분기 지점 이후 일정 범위 안에서 하드코딩 주소 후보(MOV [addr],imm32 / MOV reg,[addr])를 찾음
function Find-AddressesNear($bytes, $startOffset, $windowSize, $lo, $hi) {
    $end = [Math]::Min($bytes.Length - 9, $startOffset + $windowSize)
    $results = @()
    for ($i = $startOffset; $i -lt $end; $i++) {
        if ($bytes[$i] -eq 0xC7 -and $bytes[$i + 1] -eq 0x05) {
            $imm = [BitConverter]::ToUInt32($bytes, $i + 6)
            if ($imm -ge $lo -and $imm -lt $hi) {
                $results += [PSCustomObject]@{ FileOffset = $i; Op = "MOV [local],imm32"; Address = $imm }
            }
        }
        elseif ($bytes[$i] -eq 0x8B -and ($bytes[$i + 1] -eq 0x3D -or $bytes[$i + 1] -eq 0x35 -or $bytes[$i + 1] -eq 0x0D)) {
            $addr = [BitConverter]::ToUInt32($bytes, $i + 2)
            if ($addr -ge $lo -and $addr -lt $hi) {
                $reg = @{ 0x3D = "EDI"; 0x35 = "ESI"; 0x0D = "ECX" }[[int]$bytes[$i + 1]]
                $results += [PSCustomObject]@{ FileOffset = $i; Op = "MOV $reg,[addr]"; Address = $addr }
            }
        }
    }
    return $results
}

# 게임(CDS95.exe)의 전형적인 주소 범위. 필요하면 조정하세요.
$GameAddressLow  = 0x00400000
$GameAddressHigh = 0x00700000
$SearchWindowAfterCompare = 0x80  # CMP 지점 이후 이 바이트 범위 안에서 주소를 찾음

$bytes = [System.IO.File]::ReadAllBytes($PluginPath)
$sec = (Get-Sections $PluginPath) | Where-Object { $_.Name -eq ".text" }
if (-not $sec) { throw ".text 섹션을 찾을 수 없습니다." }

$versions = Find-VersionCompares $bytes $sec.RawPtr $sec.RawSize
if ($versions.Count -eq 0) {
    Write-Host "버전 분기(CMP EAX,imm32)를 찾지 못했습니다. 이 플러그인은 다른 패턴을 쓸 수 있습니다." -ForegroundColor Yellow
}

foreach ($v in $versions) {
    Write-Host "`n[Ver.$($v.GuessedVersion)] CMP @ 0x$($v.FileOffset.ToString('X'))" -ForegroundColor Cyan
    $addrs = Find-AddressesNear $bytes $v.FileOffset $SearchWindowAfterCompare $GameAddressLow $GameAddressHigh
    if ($addrs.Count -eq 0) {
        Write-Host "  (근처에서 하드코딩 주소를 못 찾음 - 검색 범위를 늘려보세요)" -ForegroundColor DarkGray
    }
    foreach ($a in $addrs) {
        "  0x{0:X8}  ({1}, 분기+0x{2:X})" -f $a.Address, $a.Op, ($a.FileOffset - $v.FileOffset)
    }
}
