<#
.SYNOPSIS
  GameApiKR CLI — 게임 함수/기능을 사람이 읽기 쉬운 이름으로 호출 (w12/w13).
  게임에 GameApiKR/ShipSkinKR 플러그인이 로드돼 있어야 함(로더 startup 또는 인젝션).

.DESCRIPTION
  두 가지 모드:
   1) 이름(named) 커맨드:  ./cds-api.ps1 <name> [args]    (아래 $Named 테이블)
   2) raw 함수호출:        ./cds-api.ps1 <t|c|s> <addr_hex> [ecx] [args...]   (GameApiKR 직통)

.EXAMPLE
  ./cds-api.ps1 change_ship_image 5     # 항해 배 이미지를 5(중카락)로 (다음 출항/함대편성 때 반영)
  ./cds-api.ps1 get_ship_type           # 기함 함선종류
  ./cds-api.ps1 get_ship_type 1         # 1번 슬롯 함선종류
  ./cds-api.ps1 list                    # 사용 가능한 이름 목록
  ./cds-api.ps1 t 44C6E0 5A4E18         # (raw) thiscall getType(ship0)
#>
param(
  [Parameter(Position=0)][string]$Cmd = 'help',
  [switch]$h,
  [Parameter(ValueFromRemainingArguments)][string[]]$Rest
)
$ErrorActionPreference = 'Stop'

# 각 named 커맨드 설명 (help 출력용)
$Help = [ordered]@{
  'change_ship_image' = '<0~7>  항해 배 이미지 타입 설정 (출항/함대편성 때 반영). 0코구 1카라벨 2대형카라벨 3카락 4대형카락 5중카락 6갤리온 7다우'
  'get_ship_type'     = '[slot] 함선종류 읽기 (기본 slot=0)'
  'get_sprite_cache'  = '       스프라이트 타입 캐시(0x5B3A00) 읽기'
  'select_resolution' = '<0|1|2> 시작 해상도창 선택 (0=640x480 1=800x600 2=1024x768). 키보드 UP*3->DOWN*n->Enter'
  'recover_audio'     = '       "CD 에러"(MCI 드라이버 로드불가) 복구 — 오디오 스택 리셋(관리자 자동승격). 재부팅 대체'
  'quit'              = '       게임 정상 종료(WM_CLOSE) → MCI 누수 방지. 안 죽으면 강제종료 폴백'
  'depart'            = '       출항 (상태머신 분기라 직접호출 불가 — 안내)'
  'help'              = '       이 도움말'
}

# ---- 게임 창 + 키보드 (해상도창 조작; ScreenUtilKR이 마우스만 후킹해 키보드는 실입력이 통함) ----
Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;using System.Text;
public class GK {
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb,IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h,StringBuilder s,int m);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte sc,uint f,IntPtr e);
 [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
 public delegate bool EnumWindowsProc(IntPtr h,IntPtr l);
 [StructLayout(LayoutKind.Sequential)] public struct RECT{public int left,top,right,bottom;}
}
"@ -ErrorAction SilentlyContinue

function Get-GameWindow {
  $p = Get-Process -Name cds_95 -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { return [IntPtr]::Zero }
  $script:gwh = [IntPtr]::Zero
  $cb = [GK+EnumWindowsProc]{ param($h,$l) $w=0; [void][GK]::GetWindowThreadProcessId($h,[ref]$w)
    if ($w -eq $p.Id -and [GK]::IsWindowVisible($h)) { $cn=New-Object System.Text.StringBuilder 40; [void][GK]::GetClassNameW($h,$cn,40); if($cn.ToString() -match '大航海'){ $script:gwh=$h; return $false } }
    return $true }
  [void][GK]::EnumWindows($cb,[IntPtr]::Zero); $script:gwh
}
function Send-GameKey([byte]$vk) {   # 키마다 포커스 재확보(수동검증에서 통한 방식)
  $g = Get-GameWindow; if ($g -eq [IntPtr]::Zero) { return }
  [void][GK]::SetForegroundWindow($g); Start-Sleep -Milliseconds 300
  [GK]::keybd_event($vk,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60; [GK]::keybd_event($vk,0,2,[IntPtr]::Zero); Start-Sleep -Milliseconds 300
}
function Cmd-SelectResolution($idx) {
  if ($null -eq $idx) { return "사용법: select_resolution <0|1|2>  (0=640x480 1=800x600 2=1024x768)" }
  $n = [int]$idx
  if ($n -lt 0 -or $n -gt 2) { return "0|1|2 만 가능 (0=640x480 1=800x600 2=1024x768)" }
  $g = Get-GameWindow; if ($g -eq [IntPtr]::Zero) { return "cds_95 미실행 또는 게임창 없음" }
  # 갓 시작한 해상도창은 기본 640(맨 위) 선택 상태 → DOWN*n 으로 목표(0=640,1=800,2=1024)
  if ($n -gt 0) { 1..$n | ForEach-Object { Send-GameKey 0x28 } }   # DOWN*n
  Send-GameKey 0x0D                                   # Enter → 확정
  Start-Sleep -Milliseconds 900
  $r2 = New-Object GK+RECT; [void][GK]::GetClientRect($g,[ref]$r2)
  $sz = @('640x480','800x600','1024x768')[$n]
  "OK select_resolution $n ($sz) — 창 client=$($r2.right)x$($r2.bottom)"
}

# ---- #2 예방: 정상 종료(WM_CLOSE) → 게임의 CDAudioTerminate가 _inmm MCI를 닫아 누수 방지 ----
function Cmd-Quit {
  $procs = Get-Process -Name cds_95,_CDS95 -ErrorAction SilentlyContinue
  if (-not $procs) { return "cds_95 미실행" }
  $g = Get-GameWindow
  if ($g -ne [IntPtr]::Zero) {
    [void][GK]::PostMessageW($g, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)  # WM_CLOSE
    $dl = (Get-Date).AddSeconds(6)
    while ((Get-Date) -lt $dl -and (Get-Process -Name cds_95 -ErrorAction SilentlyContinue)) { Start-Sleep -Milliseconds 300 }
  }
  $left = Get-Process -Name cds_95,_CDS95 -ErrorAction SilentlyContinue
  if ($left) { $left | Stop-Process -Force; return "정상 종료 미완 → 강제종료 폴백" }
  "OK 게임 정상 종료(WM_CLOSE) — MCI 정상 해제(누수 없음)"
}

# ---- #1 복구: "CD 에러"(MCIERR_CANNOT_LOAD_DRIVER) 시 오디오 스택 리셋(재부팅 대체). 관리자 자동승격 ----
function Cmd-RecoverAudio {
  $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
  if (-not $isAdmin) {
    $ps = @'
try {
  Restart-Service -Name Audiosrv -Force -ErrorAction Stop
  Set-Content "$env:TEMP\cds_recover_audio.log" "OK Audiosrv 재시작 완료"
} catch { Set-Content "$env:TEMP\cds_recover_audio.log" ("FAIL " + $_.Exception.Message) }
'@
    $enc = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($ps))
    Remove-Item "$env:TEMP\cds_recover_audio.log" -ErrorAction SilentlyContinue
    try { Start-Process powershell -Verb RunAs -WindowStyle Hidden -ArgumentList '-NoProfile','-EncodedCommand',$enc -ErrorAction Stop }
    catch { return "관리자 승격 취소/실패 — 수동 실행 필요: (관리자 PS) Restart-Service Audiosrv -Force" }
    $dl=(Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $dl -and -not (Test-Path "$env:TEMP\cds_recover_audio.log")) { Start-Sleep -Milliseconds 500 }
    $r = (Test-Path "$env:TEMP\cds_recover_audio.log") ? (Get-Content "$env:TEMP\cds_recover_audio.log" -Raw).Trim() : "승격 대기 시간초과"
    return "recover_audio: $r`n(참고: AudioFixKR.plugin 설치 시 CD에러 자체가 안 납니다 — 이 명령은 플러그인 없을 때 복구용)"
  }
  try { Restart-Service -Name Audiosrv -Force -ErrorAction Stop; "OK Audiosrv 재시작 완료 (오디오/MCI 상태 리셋)" }
  catch { "FAIL $($_.Exception.Message)" }
}

# ---- 저수준: GameApiKR 함수호출 ----
function Invoke-GameFunc([string]$conv, [string]$addr, [string[]]$args) {
  $cmdf = Join-Path $env:TEMP 'cds_api_cmd.txt'
  $ackf = Join-Path $env:TEMP 'cds_api_ack.txt'
  Remove-Item $ackf -ErrorAction SilentlyContinue
  $line = (@($conv, $addr) + $args) -join ' '
  Set-Content -Path $cmdf -Value $line -Encoding Ascii -NoNewline
  $dl = (Get-Date).AddSeconds(3)
  while ((Get-Date) -lt $dl) { if (Test-Path $ackf) { break }; Start-Sleep -Milliseconds 80 }
  if (Test-Path $ackf) { (Get-Content $ackf -Raw).Trim() } else { "무응답(GameApiKR 로드 확인)" }
}

# ---- 저수준: RPM 읽기/쓰기 ----
function Get-Mem([long]$addr) {
  Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;
public class RM{ [DllImport("kernel32.dll")]public static extern IntPtr OpenProcess(uint a,bool i,int p);
 [DllImport("kernel32.dll")]public static extern bool ReadProcessMemory(IntPtr h,IntPtr a,byte[] b,int s,out int r);
 [DllImport("kernel32.dll")]public static extern bool WriteProcessMemory(IntPtr h,IntPtr a,byte[] b,int s,out int w);
 [DllImport("kernel32.dll")]public static extern bool CloseHandle(IntPtr h);}
"@ -ErrorAction SilentlyContinue
  $p = Get-Process -Name cds_95 -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { throw "cds_95 미실행" }
  $h = [RM]::OpenProcess(0x10, $false, $p.Id)
  $b = New-Object byte[] 4; $r = 0; [void][RM]::ReadProcessMemory($h, [IntPtr]$addr, $b, 4, [ref]$r)
  [void][RM]::CloseHandle($h); [BitConverter]::ToInt32($b, 0)
}
function Set-Mem([long]$addr, [int]$val) {
  $p = Get-Process -Name cds_95 -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { throw "cds_95 미실행" }
  $h = [RM]::OpenProcess((0x10 -bor 0x20 -bor 0x8), $false, $p.Id)
  $b = [BitConverter]::GetBytes($val); $w = 0; [void][RM]::WriteProcessMemory($h, [IntPtr]$addr, $b, 4, [ref]$w)
  [void][RM]::CloseHandle($h)
}

# ---- 상수 ----
$SHIP_BASE = 0x5A4E18; $SHIP_STRIDE = 0x6C; $SHIP_TYPE_OFF = 0x28   # 함선종류 = base+slot*stride+0x28
$SPRITE_CACHE = 0x5B3A00                                            # 출항용 스프라이트 타입 캐시(후보)

# ---- 이름 커맨드 ----
function Cmd-ChangeShipImage($t) {
  if ($null -eq $t) { return "사용법: change_ship_image <0~7>" }
  $type = [int]$t
  if ($type -lt 0 -or $type -gt 7) { return "타입은 0~7 (0코구 1카라벨 2대형카라벨 3카락 4대형카락 5중카락 6갤리온 7다우)" }
  # ShipSkinKR 훅이 %TEMP%\cds_shiptype.txt 를 읽어 해상 스프라이트를 스킨(스탯 불변).
  # (0x5B3A00 은 스프라이트와 무관한 죽은 후보였음 — write 제거. getter call-site 리다이렉트로 반영.)
  Set-Content -Path (Join-Path $env:TEMP 'cds_shiptype.txt') -Value "$type" -Encoding Ascii -NoNewline
  "OK 배 이미지 타입=$type 설정 (약 0.2초 내 해상 스프라이트 반영, 스탯/함선종류 불변)"
}
function Cmd-GetShipType($slot) {
  $s = if ($null -eq $slot) { 0 } else { [int]$slot }
  $addr = $SHIP_BASE + $s * $SHIP_STRIDE
  Invoke-GameFunc 't' ('{0:X}' -f ($addr + 0)) @()   # getType via GameApiKR: t 44C6E0 <shipbase>
}

$Named = @{
  'change_ship_image' = { param($a) Cmd-ChangeShipImage $a[0] }
  'get_ship_type'     = { param($a)
                            $s = if ($a.Count -ge 1) { [int]$a[0] } else { 0 }
                            $t = Get-Mem (0x5A4E18 + $s * 0x6C + 0x28)   # RPM(안정) — 함선종류
                            $names = @('코구','카라벨','대형카라벨','카락','대형카락','중카락','갤리온','다우')
                            $nm = if ($t -ge 0 -and $t -lt 8) { $names[$t] } else { '(빈슬롯/-1)' }
                            "슬롯$s 함선종류 = $t ($nm)" }
  'get_sprite_cache'  = { param($a) "0x5B3A00 = " + (Get-Mem 0x5B3A00) }
  'select_resolution' = { param($a) Cmd-SelectResolution $a[0] }
  'recover_audio'     = { param($a) Cmd-RecoverAudio }
  'quit'              = { param($a) Cmd-Quit }
  'depart'            = { param($a)
                            # 출항은 단일 함수가 아니라 메인 상태머신 분기(0x4936Cx~0x493705, 현재도시=-1 write).
                            # 직접 call 불가 → 상태 세팅/UI 필요. 확실한 자동화는 quit 후 재시작(로드→출항)이거나
                            # ScreenUtilKR로 포트메뉴 '출항' 클릭. (아래는 상태만 흉내내는 실험적 write — 미완)
                            "출항은 상태머신 분기라 직접 호출 불가. 자동화는 quit+재시작 또는 UI클릭 사용. (트레이스: write@0x493705)" }
}

# ---- 디스패치 ----
if ($h -or $Cmd -in 'help', 'list', '-h', '--help', '?') {
  "cds-api.ps1 — 게임 함수/기능 CLI (GameApiKR)"
  ""
  "사용법:  .\cds-api.ps1 <커맨드> [인자]"
  ""
  "named 커맨드:"
  foreach ($k in $Help.Keys) { "  {0,-18} {1}" -f $k, $Help[$k] }
  ""
  "raw 함수호출:  .\cds-api.ps1 <t|c|s> <addr_hex> [ecx] [args...]   (t=thiscall c=cdecl s=stdcall)"
  "예:  .\cds-api.ps1 change_ship_image 5     .\cds-api.ps1 get_ship_type 1     .\cds-api.ps1 t 44C6E0 5A4E18"
}
elseif ($Named.ContainsKey($Cmd)) {
  & $Named[$Cmd] $Rest
}
elseif ($Cmd -in 't', 'c', 's') {
  Invoke-GameFunc $Cmd $Rest[0] ($Rest | Select-Object -Skip 1)
}
else {
  "알 수 없는 커맨드 '$Cmd'.  ./cds-api.ps1 list  로 이름 목록 확인."
}
