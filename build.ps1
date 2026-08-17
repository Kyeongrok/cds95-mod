<#
.SYNOPSIS
    plugins-src의 플러그인들을 빌드하고, 지정한 게임 폴더의 CDS95Util로 복사합니다.

.PARAMETER GamePath
    실제 CDS95 게임 설치 폴더. 이 폴더 아래에 CDS95Util\ 가 있어야 합니다.
    기본값은 아래 $GamePath 변수를 직접 고쳐서 쓰면 됩니다.

.PARAMETER Configuration
    빌드 구성 (Release/Debug). 기본값 Release.

.PARAMETER PluginDir
    플러그인을 둘 폴더. 안 주면 CDS95Util\plugins\<만든이>\ 아래에 KR 플러그인이 이미
    있는 폴더를 찾아 거기로, 없으면 CDS95Util 로 배포합니다. 데이터 파일(json)은
    언제나 CDS95Util 에 둡니다.

.PARAMETER SkipDeploy
    빌드만 하고 게임 폴더로 복사하지 않습니다.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -GamePath "D:\Games\cds95" -Configuration Debug
#>
param(
    [string]$GamePath = "C:\Users\Administrator\Downloads\cds95",
    [string]$Configuration = "Release",
    [string]$PluginDir = "",
    [switch]$SkipDeploy
)

$ErrorActionPreference = "Stop"

$RepoRoot   = $PSScriptRoot
$PluginsSrc = Join-Path $RepoRoot "plugins-src"
$BuildDir   = Join-Path $PluginsSrc "build"
$MinHookDir = Join-Path $PluginsSrc "third_party\minhook"

# 여기서 빌드할 플러그인 타깃과 결과물 파일명을 나열합니다.
# 새 플러그인을 plugins-src에 추가하면 이 목록에도 추가하세요.
$PluginTargets = @("HotelUtilKR", "TradeUtilKR", "CharacterUtilKR", "WorldMapKR", "ShipSkinKR", "PatchUtilKR", "ModUtilKR", "QuestModKR", "UpdateUtilKR", "FatigueUtilKR", "HotkeyUtilKR", "HintUtilKR", "MarketUtilKR", "SaveUtilKR", "CityPicKR", "DialogUtilKR", "SkillUtilKR", "BookUtilKR", "ShipInfoKR", "ButtonMakerKR", "LandWarKR")

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
    # CDS95Util 까지 있어야 진짜 배포 대상이다. cds_95.exe 만 보고 고르면
    # 플러그인을 안 쓰는 사본(예: 통합정정판 원본 폴더)을 집어 배포가 통째로 건너뛰어진다.
    $detected = Get-ChildItem "$env:USERPROFILE\Desktop" -Directory -ErrorAction SilentlyContinue |
        Where-Object { (Test-Path (Join-Path $_.FullName "cds_95.exe")) -and
                       (Test-Path (Join-Path $_.FullName "CDS95Util")) } |
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

$UtilDir = Join-Path $GamePath "CDS95Util"
if (-not (Test-Path $UtilDir)) {
    Write-Host "`n$UtilDir 가 없습니다. 게임 경로($GamePath)가 맞는지 확인하세요." -ForegroundColor Yellow
    return
}

# ---- 배포처 고르기 ----
# 로더는 CDS95Util 뿐 아니라 CDS95Util\plugins\<만든이>\ 아래도 훑는다
# (QuestModKR 의 UpToDataDir 주석 참고 — 데이터 파일만 CDS95Util 한 자리에 모은다).
# 우리 플러그인이 이미 그런 하위 폴더에 있으면 거기가 진짜 배포처다.
# ★ 두 자리에 같은 이름이 다 있으면 같은 DLL 이 두 번 로드된다. 그러면 감시 스레드가
#   둘 다 게임 창을 서브클래싱해 나중에 건 쪽이 바깥이 되는데, 1초 폴링 경쟁이라
#   켤 때마다 승자가 바뀐다 — 새 기능이 "됐다 안 됐다" 하는 원인이다(2026-08-11).
if ($PluginDir) {
    $TargetDir = if ([IO.Path]::IsPathRooted($PluginDir)) { $PluginDir } else { Join-Path $UtilDir $PluginDir }
} else {
    $subs = @(Get-ChildItem (Join-Path $UtilDir "plugins") -Directory -ErrorAction SilentlyContinue |
              Where-Object { Get-ChildItem $_.FullName -Filter "*KR.plugin" -File -ErrorAction SilentlyContinue })
    if ($subs.Count -gt 1) {
        throw "CDS95Util\plugins 아래에 KR 플러그인 폴더가 여러 개입니다: $($subs.Name -join ', ')`n-PluginDir 로 하나를 골라 주세요."
    }
    $TargetDir = if ($subs.Count -eq 1) { $subs[0].FullName } else { $UtilDir }
}
if (-not (Test-Path $TargetDir)) { New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null }

Write-Step "배포 대상: $TargetDir"
if ($TargetDir -ne $UtilDir) {
    Write-Host "  (데이터 파일 cities.json / patches.json 등은 $UtilDir 에 둡니다)" -ForegroundColor DarkGray
}

# 게임이 켜져 있으면 .plugin 은 프로세스에 매핑돼 있어 덮어쓸 수 없다.
# 하지만 이름 바꾸기는 된다(로더가 FILE_SHARE_DELETE 로 열어 둔다).
# 그래서 낡은 것을 옆으로 밀어내고 새 것을 제자리에 둔다 —
# 실행 중인 게임은 밀려난 파일을 그대로 쓰고, 다음에 게임을 켜면 새 것이 로드된다.
# 덕분에 배포하려고 게임을 끌 필요가 없다(반영은 다음 실행부터).
$staged = @()
foreach ($plugin in $BuiltPlugins) {
    $name = Split-Path $plugin -Leaf
    $dst  = Join-Path $TargetDir $name
    try {
        Copy-Item $plugin -Destination $dst -Force -ErrorAction Stop
        Write-Host "복사 완료: $dst" -ForegroundColor Green
    } catch {
        try {
            $old = "$name.old-" + (Get-Date -Format "yyyyMMddHHmmss")
            Rename-Item -Path $dst -NewName $old -ErrorAction Stop
            Copy-Item $plugin -Destination $dst -Force -ErrorAction Stop
            $staged += $name
            Write-Host "교체 예약: $dst  (실행 중이라 낡은 것은 $old 로 밀어냄)" -ForegroundColor Yellow
        } catch {
            Write-Host "복사 실패: $dst  ($($_.Exception.Message))" -ForegroundColor Red
        }
    }
}

# 플러그인이 옆에서 읽는 데이터 파일. 이미 있으면 사용자가 고쳐 놨을 수 있으므로 덮지 않는다.
$DataFiles = @(
    (Join-Path $PluginsSrc "WorldMapKR\cities.json"),
    (Join-Path $PluginsSrc "WorldMapKR\discoveries.json"),
    (Join-Path $PluginsSrc "PatchUtilKR\patches.json")
)
foreach ($data in $DataFiles) {
    if (-not (Test-Path $data)) { continue }
    $dst = Join-Path $UtilDir (Split-Path $data -Leaf)
    if (Test-Path $dst) {
        Write-Host "그대로 둠: $dst (이미 있음 — 고쳐 쓴 것을 덮지 않는다)" -ForegroundColor DarkGray
    } else {
        Copy-Item $data -Destination $dst -Force
        Write-Host "복사 완료: $dst" -ForegroundColor Green
    }
}

# DialogUtilKR 이 읽는 대사 파일은 폴더째 둔다 (CDS95Util\dialogs\*.json).
# 여기도 이미 있는 파일은 덮지 않는다 — 사용자가 고쳐 쓴 대사를 지우면 안 된다.
$DialogSrc = Join-Path $PluginsSrc "DialogUtilKR\dialogs"
if (Test-Path $DialogSrc) {
    $DialogDst = Join-Path $UtilDir "dialogs"
    if (-not (Test-Path $DialogDst)) { New-Item -ItemType Directory -Path $DialogDst | Out-Null }
    foreach ($f in (Get-ChildItem $DialogSrc -Filter "*.json" -File)) {
        $dst = Join-Path $DialogDst $f.Name
        if (Test-Path $dst) {
            Write-Host "그대로 둠: $dst (이미 있음 — 고쳐 쓴 것을 덮지 않는다)" -ForegroundColor DarkGray
        } else {
            Copy-Item $f.FullName -Destination $dst -Force
            Write-Host "복사 완료: $dst" -ForegroundColor Green
        }
    }
}

# 지난번에 밀어낸 파일 중 이제 안 잡혀 있는 것들을 치운다.
Get-ChildItem $TargetDir -Filter "*.old-*" -ErrorAction SilentlyContinue | ForEach-Object {
    try { Remove-Item $_.FullName -Force -ErrorAction Stop } catch { }
}

# ---- 하위 폴더에 배포했을 때: ModUtilKR 만은 루트에도 둔다 ----
# 로더(ddraw.dll, 2019년 원본)는 CDS95Util 루트의 *.plugin 만 불러온다.
# plugins\<만든이>\ 아래 것은 ModUtilKR 이 LoadSubPlugins 로 대신 불러온다.
# 그러니 ModUtilKR 이 루트에 없으면 하위 폴더가 통째로 안 뜬다 (2026-08-11 이렇게 깨뜨렸다).
if ($TargetDir -ne $UtilDir) {
    $loader = $BuiltPlugins | Where-Object { (Split-Path $_ -Leaf) -eq "ModUtilKR.plugin" }
    if ($loader) {
        $dst = Join-Path $UtilDir "ModUtilKR.plugin"
        try {
            Copy-Item $loader -Destination $dst -Force -ErrorAction Stop
        } catch {
            $old = "ModUtilKR.plugin.old-" + (Get-Date -Format "yyyyMMddHHmmss")
            Rename-Item -Path $dst -NewName $old -ErrorAction SilentlyContinue
            Copy-Item $loader -Destination $dst -Force -ErrorAction SilentlyContinue
            $staged += "ModUtilKR.plugin"
        }
        Write-Host "복사 완료: $dst  (하위 폴더를 불러오는 로더라 루트에도 둔다)" -ForegroundColor Green
    }
}

# ---- 딴 자리에 남은 같은 이름 알리기 ----
# 루트와 하위 폴더에 같은 이름이 다 있으면 같은 DLL 이 두 번 로드될 수 있다.
# 고친 ModUtilKR 은 루트에 있는 이름을 건너뛰므로 안전하지만, 옛 ModUtilKR 이 돌고 있으면
# 여전히 겹친다. 지우지 않고 알리기만 한다 — 어느 쪽을 남길지는 사람이 정할 일이다.
$otherDirs = @($UtilDir) + @(Get-ChildItem (Join-Path $UtilDir "plugins") -Directory -ErrorAction SilentlyContinue |
                             ForEach-Object { $_.FullName })
$dups = @()
foreach ($dir in $otherDirs) {
    if ((Resolve-Path $dir).Path -eq (Resolve-Path $TargetDir).Path) { continue }
    foreach ($plugin in $BuiltPlugins) {
        $name = Split-Path $plugin -Leaf
        if ($name -eq "ModUtilKR.plugin") { continue }   # 루트에 있어야 하는 것
        $other = Join-Path $dir $name
        if (Test-Path $other) { $dups += $other }
    }
}
if ($dups.Count -gt 0) {
    Write-Host "`n같은 이름이 딴 자리에도 있습니다 — 한쪽을 .plugin.off 로 꺼 두세요:" -ForegroundColor Yellow
    $dups | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkGray }
}

if ($staged.Count -gt 0) {
    Write-Host "`n게임이 실행 중이라 다음 실행부터 반영됩니다: $($staged -join ', ')" -ForegroundColor Cyan
}

Write-Host "`n게임을 실행하고 DebugView로 로그를 확인하세요. (plugins-src/DebugView.md 참고)" -ForegroundColor Cyan
