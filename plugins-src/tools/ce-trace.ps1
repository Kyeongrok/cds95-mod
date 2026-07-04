<#
.SYNOPSIS
  Cheat Engine "find what accesses" 자동화 하네스 (w2).
  CE autorun 의 cds-trace.lua 와 job 파일로 통신한다. CE는 커널 디버거라 게임을 안 죽인다
  (유저모드 HW-BP find-what-accesses.py 는 이 게임에서 크래시 → 폐기).

.DESCRIPTION
  1) job 파일(%TEMP%\cds_trace_job.txt) 작성
  2) Cheat Engine 실행 → autorun cds-trace.lua 가 job 감지 → openProcess + 접근 BP + N초 수집
  3) 결과 파일(%TEMP%\cds_trace_out.txt)에 "=== DONE ===" 뜰 때까지 대기 후 출력
  4) CE 종료

  ★ 사전 1회: cds-trace.lua 를 CE autorun 폴더에 설치해야 함(-Install 로 자동).
  ★ 게임(cds_95.exe)이 실행 중이어야 함. BP 수집 창(N초) 동안 대상 접근을 유발(재입항 등).

.EXAMPLE
  ./ce-trace.ps1 -Install
  ./ce-trace.ps1 -Addr 5A4E40 -Seconds 25 -Trigger access
#>
param(
  [string]$Addr,
  [int]$Seconds = 25,
  [ValidateSet("access","write")][string]$Trigger = "access",
  [int]$Size = 4,
  [string]$Proc = "cds_95.exe",
  [switch]$Install,
  [string]$CePath = "C:\Program Files\Cheat Engine\Cheat Engine.exe",
  [string]$CeAutorun = "C:\Program Files\Cheat Engine\autorun"
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($Install) {
  Copy-Item (Join-Path $here "cds-trace.lua") (Join-Path $CeAutorun "cds-trace.lua") -Force
  Write-Host "설치 완료: $CeAutorun\cds-trace.lua" -ForegroundColor Green
  if (-not $Addr) { return }
}
if (-not $Addr) { throw "-Addr 필요 (예: -Addr 5A4E40). 최초엔 -Install 먼저." }

# autorun 최신본 동기화(레포 수정분 반영)
try { Copy-Item (Join-Path $here "cds-trace.lua") (Join-Path $CeAutorun "cds-trace.lua") -Force } catch {}

if (-not (Get-Process -Name $Proc.Replace(".exe","") -ErrorAction SilentlyContinue)) {
  throw "$Proc 실행중 아님. 게임 켜고 세이브 로드 후 다시."
}

$job = Join-Path $env:TEMP "cds_trace_job.txt"
$out = Join-Path $env:TEMP "cds_trace_out.txt"
Remove-Item $out -ErrorAction SilentlyContinue
@($Addr, $Size, $Trigger, $Seconds, $out, $Proc) | Set-Content -Path $job -Encoding Ascii
Write-Host "job: addr=$Addr size=$Size trigger=$Trigger seconds=$Seconds -> $out" -ForegroundColor DarkGray

# ★ CE를 절대 강제종료하지 않는다: CE는 kill-on-exit 기본값이라 force-kill 하면 디버깅 중인
#   게임까지 죽는다. lua 의 closeCE()로 CE가 스스로 정상 종료(클린 detach)하게 둔다.
if (Get-Process -Name "cheatengine*" -ErrorAction SilentlyContinue) {
  throw "CE가 이미 실행 중입니다. autorun은 CE '시작' 시에만 도니, CE를 먼저 정상 종료(창 닫기)한 뒤 다시 실행하세요."
}

Add-Type @"
using System; using System.Runtime.InteropServices;
public class Fgw { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); }
"@
Start-Process -FilePath $CePath
Write-Host "CE 실행됨. ~4초 후 BP 무장. 게임을 앞으로 되돌립니다 — 그 사이 $Seconds초간 게임에서 대상 접근을 유발하세요(편성/정보 열기·재입항 등)." -ForegroundColor Cyan

# CE 초기화 + autorun defer(1.5s) + arm 대기 후, 게임 창을 포그라운드로(사용자가 조작 가능하게)
Start-Sleep -Seconds 4
$g = Get-Process -Name $Proc.Replace(".exe","") -ErrorAction SilentlyContinue | Select-Object -First 1
if ($g -and $g.MainWindowHandle -ne 0) { [Fgw]::SetForegroundWindow($g.MainWindowHandle) | Out-Null; Write-Host "게임 포그라운드로 전환됨. 지금 조작하세요." -ForegroundColor Green }

# DONE 대기 — CE 부팅(드라이버 로드)이 느릴 수 있어 버퍼 넉넉히 + CE 자기종료도 완료 신호로.
$deadline = (Get-Date).AddSeconds($Seconds + 45)
$done = $false
while ((Get-Date) -lt $deadline) {
  if (Test-Path $out) {
    $c = Get-Content $out -Raw -ErrorAction SilentlyContinue
    if ($c -and $c.Contains("=== DONE ===")) { $done = $true; break }
  }
  # CE가 스스로 닫혔으면(closeCE) 수집 끝 — 파일 한 번 더 확인 후 종료
  if (-not (Get-Process -Name "cheatengine*" -ErrorAction SilentlyContinue)) {
    Start-Sleep -Milliseconds 500
    $c = Get-Content $out -Raw -ErrorAction SilentlyContinue
    if ($c -and $c.Contains("=== DONE ===")) { $done = $true }
    break
  }
  Start-Sleep -Milliseconds 500
}

Write-Host "`n===== 결과 =====" -ForegroundColor Yellow
if (Test-Path $out) { Get-Content $out } else { Write-Host "결과 파일 없음 — autorun 미실행? (-Install 확인)" -ForegroundColor Red }
if (-not $done) { Write-Host "(주의: DONE 마커 못 봄)" -ForegroundColor Red }
Write-Host "CE는 스스로 종료됩니다(closeCE). 게임 프로세스 유지." -ForegroundColor DarkGray
