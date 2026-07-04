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
  [Parameter(Mandatory)][string]$Cmd,
  [Parameter(ValueFromRemainingArguments)][string[]]$Rest
)
$ErrorActionPreference = 'Stop'

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
  # ShipSkinKR 훅용 파일 + 스프라이트 캐시 직접 write (둘 다) — 반영은 출항/함대편성 때
  Set-Content -Path (Join-Path $env:TEMP 'cds_shiptype.txt') -Value "$type" -Encoding Ascii -NoNewline
  try { Set-Mem $SPRITE_CACHE $type } catch {}
  "OK 배 이미지 타입=$type 설정 (다음 출항 또는 함대편성 기함변경 때 반영)"
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
  'quit'              = { param($a)
                            Get-Process -Name cds_95,_CDS95 -ErrorAction SilentlyContinue | Stop-Process -Force
                            "OK 게임 종료(프로세스 강제종료)" }
  'depart'            = { param($a)
                            # 출항은 단일 함수가 아니라 메인 상태머신 분기(0x4936Cx~0x493705, 현재도시=-1 write).
                            # 직접 call 불가 → 상태 세팅/UI 필요. 확실한 자동화는 quit 후 재시작(로드→출항)이거나
                            # ScreenUtilKR로 포트메뉴 '출항' 클릭. (아래는 상태만 흉내내는 실험적 write — 미완)
                            "출항은 상태머신 분기라 직접 호출 불가. 자동화는 quit+재시작 또는 UI클릭 사용. (트레이스: write@0x493705)" }
}

# ---- 디스패치 ----
if ($Cmd -eq 'list') {
  "== named 커맨드 =="; $Named.Keys | Sort-Object | ForEach-Object { "  $_" }
  "== raw ==  ./cds-api.ps1 <t|c|s> <addr_hex> [ecx] [args...]"
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
