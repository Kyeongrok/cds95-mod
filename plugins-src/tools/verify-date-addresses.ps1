<#
.SYNOPSIS
    한국어판(cds_95.exe) 라이브 메모리의 게임 내 현재 년/월/일 주소를 검증한다.
    HotelUtil 숙박일수 이식 작업(fb1)에서 차등 스캔으로 확정한 주소.

.DESCRIPTION
    ★ 확정된 주소 (2026-07-02, 차등 스캔 find-date-by-diff.ps1 로 검증) ★
    라이브 메모리는 세이브 파일과 달리 년/월/일을 각각 32비트 정수(DWORD)로 연속 저장한다:

        Year  (i32) = 0x005A4D20
        Month (i32) = 0x005A4D24
        Day   (i32) = 0x005A4D28

    (세이브 파일은 u16 year @0x15 + u8 month @0x19 + u8 day @0x1A 로 폭/간격이 다름.
     그래서 세이브 오프셋 기반으로 유도한 초기 가설 0x005B6112 계열은 전부 틀렸었음.)

    동작: cds_95 프로세스에 붙어 위 3개 주소를 i32로 읽고, 기대값과 비교해 PASS/FAIL.
    기대값은 -Year/-Month/-Day 로 직접 주거나, 안 주면 SAVEDATA.CDS 에서 읽는다
    (단 라이브 게임 상태가 디스크 세이브와 같을 때만 일치함 — 보통은 게임 화면 날짜를 직접 지정).

.EXAMPLE
    .\verify-date-addresses.ps1 -Year 1480 -Month 7 -Day 9
#>
param(
    [string]$SavePath = "$env:USERPROFILE\Desktop\대항해시대3\SAVEDATA.CDS",
    [int]$Year = -1,
    [int]$Month = -1,
    [int]$Day = -1
)

$ErrorActionPreference = "Stop"

# --- 확정 주소 (i32) ---
$YearAddr   = 0x005A4D20
$MonthAddr  = 0x005A4D24
$DayAddr    = 0x005A4D28
$ProcessName = "cds_95"

# --- 기대값: 파라미터로 안 주면 세이브 파일에서 읽음 ---
if ($Year -lt 0 -or $Month -lt 0 -or $Day -lt 0) {
    if (-not (Test-Path $SavePath)) {
        throw "SAVEDATA.CDS 없음: $SavePath  (-SavePath 지정하거나 -Year/-Month/-Day 직접 지정)"
    }
    $save = [System.IO.File]::ReadAllBytes($SavePath)
    $Year  = [BitConverter]::ToUInt16($save, 0x15)
    $Month = $save[0x19]
    $Day   = $save[0x1A]
    Write-Host "기대값 (SAVEDATA.CDS): $Year 년 $Month 월 $Day 일  ※ 게임이 이 세이브 상태일 때만 유효" -ForegroundColor DarkGray
} else {
    Write-Host "기대값 (직접 지정): $Year 년 $Month 월 $Day 일" -ForegroundColor DarkGray
}

if (-not ("Cds.Mem" -as [type])) {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
namespace Cds {
    public static class Mem {
        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32.dll", SetLastError=true)]
        public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out int read);
        [DllImport("kernel32.dll")]
        public static extern bool CloseHandle(IntPtr h);
    }
}
"@
}

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) {
    Write-Host "'$ProcessName' 프로세스 없음. 게임 실행 + 세이브 로드 후 다시 실행." -ForegroundColor Yellow
    exit 1
}
Write-Host "프로세스 발견: PID=$($proc.Id)" -ForegroundColor DarkGray

$h = [Cds.Mem]::OpenProcess(0x0010 -bor 0x0400, $false, $proc.Id)
if ($h -eq [IntPtr]::Zero) {
    throw "OpenProcess 실패. Win32Error=$([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
}

function Read-I32([long]$addr) {
    $buf = New-Object byte[] 4
    $read = 0
    $ok = [Cds.Mem]::ReadProcessMemory($h, [IntPtr]$addr, $buf, 4, [ref]$read)
    if (-not $ok -or $read -ne 4) { return $null }
    return [BitConverter]::ToInt32($buf, 0)
}

try {
    $liveYear  = Read-I32 $YearAddr
    $liveMonth = Read-I32 $MonthAddr
    $liveDay   = Read-I32 $DayAddr

    if ($null -eq $liveYear -or $null -eq $liveMonth -or $null -eq $liveDay) {
        Write-Host "라이브 주소 읽기 실패 (세이브 미로드 상태이면 0/읽기실패일 수 있음)." -ForegroundColor Red
        exit 1
    }

    Write-Host ""
    Write-Host ("{0,-8} {1,-14} {2,-8} {3,-8} {4}" -f "필드", "주소(i32)", "기대", "라이브", "결과")
    Write-Host ("-" * 52)
    $rY = if ($liveYear  -eq $Year)  { "PASS" } else { "FAIL" }
    $rM = if ($liveMonth -eq $Month) { "PASS" } else { "FAIL" }
    $rD = if ($liveDay   -eq $Day)   { "PASS" } else { "FAIL" }
    $cY = if ($rY -eq "PASS") { "Green" } else { "Red" }
    $cM = if ($rM -eq "PASS") { "Green" } else { "Red" }
    $cD = if ($rD -eq "PASS") { "Green" } else { "Red" }
    Write-Host ("{0,-8} 0x{1:X8}   {2,-8} {3,-8} {4}" -f "Year",  $YearAddr,  $Year,  $liveYear,  $rY) -ForegroundColor $cY
    Write-Host ("{0,-8} 0x{1:X8}   {2,-8} {3,-8} {4}" -f "Month", $MonthAddr, $Month, $liveMonth, $rM) -ForegroundColor $cM
    Write-Host ("{0,-8} 0x{1:X8}   {2,-8} {3,-8} {4}" -f "Day",   $DayAddr,   $Day,   $liveDay,   $rD) -ForegroundColor $cD

    if ($rY -eq "PASS" -and $rM -eq "PASS" -and $rD -eq "PASS") {
        Write-Host "`n>>> 검증 성공: 년/월/일 라이브 주소 확정 (각 i32)." -ForegroundColor Green
        exit 0
    } else {
        Write-Host "`n>>> 불일치. 게임에 표시된 실제 날짜를 -Year/-Month/-Day 로 정확히 넘겼는지 확인하거나," -ForegroundColor Yellow
        Write-Host "    빌드가 바뀌었다면 find-date-by-diff.ps1 로 주소를 다시 찾으세요." -ForegroundColor Yellow
        exit 1
    }
}
finally {
    [void][Cds.Mem]::CloseHandle($h)
}
