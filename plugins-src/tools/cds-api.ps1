<#
.SYNOPSIS
  GameApiKR — 게임 내부 함수를 PowerShell에서 호출하는 CLI (w12).
  게임에 GameApiKR.plugin 이 로드돼 있어야 함(로더 startup 또는 인젝션).

.DESCRIPTION
  %TEMP%\cds_api_cmd.txt 에 명령을 쓰고 %TEMP%\cds_api_ack.txt 결과를 읽는다.
  호출규약: t=__thiscall(첫 인자가 this/ecx), c=__cdecl, s=__stdcall. 주소·인자는 hex.

.EXAMPLE
  # thiscall getType(this=0x5A4E18) = 기함 함선종류
  ./cds-api.ps1 t 44C6E0 5A4E18
  # cdecl func(1, 2)
  ./cds-api.ps1 c 401000 1 2
#>
param(
  [Parameter(Mandatory)][ValidateSet('t','c','s')][string]$Conv,
  [Parameter(Mandatory)][string]$Addr,
  [Parameter(ValueFromRemainingArguments)][string[]]$Args
)
$cmd = Join-Path $env:TEMP 'cds_api_cmd.txt'
$ack = Join-Path $env:TEMP 'cds_api_ack.txt'
Remove-Item $ack -ErrorAction SilentlyContinue
$line = (@($Conv, $Addr) + $Args) -join ' '
Set-Content -Path $cmd -Value $line -Encoding Ascii -NoNewline
$deadline = (Get-Date).AddSeconds(3)
while ((Get-Date) -lt $deadline) {
  if (Test-Path $ack) { break }
  Start-Sleep -Milliseconds 100
}
if (Test-Path $ack) { Get-Content $ack -Raw }
else { Write-Host "무응답 — GameApiKR 로드됐는지 확인" -ForegroundColor Red }
