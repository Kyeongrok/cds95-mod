<#
.SYNOPSIS
    plugins-src의 플러그인들을 빌드하고, 지정한 게임 폴더의 CDS95Util로 복사합니다.

.PARAMETER GamePath
    실제 CDS95 게임 설치 폴더. 이 폴더 아래에 CDS95Util\ 가 있어야 합니다.
    기본값은 아래 $GamePath 변수를 직접 고쳐서 쓰면 됩니다.

.PARAMETER Configuration
    빌드 구성 (Release/Debug). 기본값 Release.

.PARAMETER SkipDeploy
    빌드만 하고 게임 폴더로 복사하지 않습니다.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -GamePath "D:\Games\cds95" -Configuration Debug
#>
param(
    [string]$GamePath = "C:\Users\Administrator\Downloads\cds95",
    [string]$Configuration = "Release",
    [switch]$SkipDeploy
)

$ErrorActionPreference = "Stop"

$RepoRoot   = $PSScriptRoot
$PluginsSrc = Join-Path $RepoRoot "plugins-src"
$BuildDir   = Join-Path $PluginsSrc "build"
$MinHookDir = Join-Path $PluginsSrc "third_party\minhook"

# 여기서 빌드할 플러그인 타깃과 결과물 파일명을 나열합니다.
# 새 플러그인을 plugins-src에 추가하면 이 목록에도 추가하세요.
$PluginTargets = @("CollectionUtilKR", "HotelUtilKR", "TradeUtilKR", "CharacterUtilKR")

function Write-Step($msg) {
    Write-Host "==> $msg" -ForegroundColor Cyan
}

# ---- MinHook 서브모듈 확인 ----
if (-not (Test-Path (Join-Path $MinHookDir "include\MinHook.h"))) {
    Write-Step "MinHook 서브모듈이 없어 초기화합니다"
    git -C $RepoRoot submodule update --init --recursive
}
if (-not (Test-Path (Join-Path $MinHookDir "include\MinHook.h"))) {
    throw "MinHook 소스를 찾을 수 없습니다: $MinHookDir`n`n다음 명령으로 먼저 추가하세요:`n  git submodule add https://github.com/TsudaKageyu/minhook.git plugins-src/third_party/minhook"
}

# ---- CMake 구성 (32비트 필수: CDS95.exe가 32비트 프로세스) ----
Write-Step "CMake 구성 (x86 / Win32)"
cmake -S $PluginsSrc -B $BuildDir -A Win32
if ($LASTEXITCODE -ne 0) { throw "CMake 구성 실패 (exit $LASTEXITCODE)" }

# ---- 빌드 ----
Write-Step "빌드 ($Configuration)"
cmake --build $BuildDir --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "빌드 실패 (exit $LASTEXITCODE)" }

# ---- 결과물 확인 ----
$BuiltPlugins = @()
foreach ($target in $PluginTargets) {
    $outputPath = Join-Path $BuildDir "$target\$Configuration\$target.plugin"
    if (-not (Test-Path $outputPath)) {
        throw "빌드 결과물을 찾을 수 없습니다: $outputPath"
    }
    Write-Host "빌드 완료: $outputPath" -ForegroundColor Green
    $BuiltPlugins += $outputPath
}

# ---- 게임 폴더로 배포 ----
if ($SkipDeploy) {
    Write-Host "`n-SkipDeploy 지정됨 - 배포는 건너뜁니다." -ForegroundColor Yellow
    return
}

if (-not $GamePath -or -not (Test-Path $GamePath)) {
    # 기본 경로가 없으면 Desktop 하위에서 cds_95.exe 가 있는 폴더를 자동 탐지한다.
    # (build.ps1 기본값이 다른 PC 경로라 배포가 조용히 건너뛰어져 게임 폴더에 구버전이
    #  남는 stale 배포 사고 방지 — fb30 "이스탄불 양모/어육"의 진짜 원인이었음.)
    $detected = Get-ChildItem "$env:USERPROFILE\Desktop" -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName "cds_95.exe") } |
        Select-Object -First 1
    if ($detected) {
        $GamePath = $detected.FullName
        Write-Host "게임 폴더 자동 탐지: $GamePath" -ForegroundColor DarkGray
    }
}

if (-not $GamePath -or -not (Test-Path $GamePath)) {
    Write-Host "`n게임 경로를 찾을 수 없어 배포를 건너뜁니다: $GamePath" -ForegroundColor Yellow
    Write-Host "스크립트 상단의 `$GamePath 기본값을 실제 경로로 고치거나 -GamePath 로 넘겨주세요." -ForegroundColor Yellow
    return
}

$TargetDir = Join-Path $GamePath "CDS95Util"
if (-not (Test-Path $TargetDir)) {
    Write-Host "`n$TargetDir 가 없습니다. 게임 경로($GamePath)가 맞는지 확인하세요." -ForegroundColor Yellow
    return
}

Write-Step "배포 대상: $TargetDir"
foreach ($plugin in $BuiltPlugins) {
    Copy-Item $plugin -Destination $TargetDir -Force
    Write-Host "복사 완료: $TargetDir\$(Split-Path $plugin -Leaf)" -ForegroundColor Green
}

Write-Host "`n게임을 실행하고 DebugView로 로그를 확인하세요. (plugins-src/DebugView.md 참고)" -ForegroundColor Cyan
