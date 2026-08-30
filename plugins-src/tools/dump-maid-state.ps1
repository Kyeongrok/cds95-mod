<#
.SYNOPSIS
    실행 중인 CDS_95 에서 여급 127명의 "살아 있는" 상태를 읽어 표로 보여줍니다.

.DESCRIPTION
    여급 런타임 객체 배열은 0x5B3C60 부터 60바이트 x 127칸이고, 칸마다
    +0x00 vtable(0x518ED0) · +0x04 종류(2) · +0x20 친밀도 · +0x24 지금 도시 가 있습니다.
    +0x20 ~ +0x38 은 게임이 세이브에 그대로 넣고 뺍니다(직렬화 0x4796B0 / 0x479630).

    어느 필드가 "만났다"를 뜻하는지 확인할 때는 -Save 로 스냅샷을 떠 두고,
    게임에서 여급에게 말을 건 다음 -Diff 로 견주면 됩니다.

.EXAMPLE
    .\dump-maid-state.ps1                       # 값이 초기값과 다른 칸만
    .\dump-maid-state.ps1 -All                  # 127칸 전부
    .\dump-maid-state.ps1 -Save before.csv      # 스냅샷 저장
    .\dump-maid-state.ps1 -Diff before.csv      # 그 뒤로 바뀐 칸만
#>
param(
    [switch]$All,
    [string]$Save = "",
    [string]$Diff = ""
)

$ErrorActionPreference = "Stop"

$MAID_BASE  = 0x5B3C60
$MAID_SZ    = 60
$MAID_COUNT = 127
$MAID_VT    = 0x518ED0

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class CdsMem {
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int a, bool b, int pid);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, int size, out int read);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  public static byte[] Read(int pid, int addr, int len) {
    IntPtr h = OpenProcess(0x0010, false, pid);       // PROCESS_VM_READ
    if (h == IntPtr.Zero) return null;
    byte[] b = new byte[len]; int r;
    bool ok = ReadProcessMemory(h, (IntPtr)addr, b, len, out r);
    CloseHandle(h);
    return ok ? b : null;
  }
}
'@

$proc = Get-Process CDS_95 -ErrorAction SilentlyContinue
if (-not $proc) { throw "CDS_95 가 실행 중이 아닙니다. 게임을 켜고 세이브를 불러온 뒤에 쓰세요." }

$buf = [CdsMem]::Read($proc.Id, $MAID_BASE, ($MAID_SZ * $MAID_COUNT))
if (-not $buf) { throw "메모리를 읽지 못했습니다(권한 또는 주소)." }

$rows = @()
for ($i = 0; $i -lt $MAID_COUNT; $i++) {
    $o = $i * $MAID_SZ
    $rows += [pscustomobject]@{
        row      = $i
        vt       = "0x{0:X}" -f [BitConverter]::ToInt32($buf, $o)
        kind     = [BitConverter]::ToInt32($buf, $o + 0x04)
        intimacy = [BitConverter]::ToInt32($buf, $o + 0x20)
        city     = [BitConverter]::ToInt32($buf, $o + 0x24)
        f28      = [BitConverter]::ToInt32($buf, $o + 0x28)
        f2c      = [BitConverter]::ToInt32($buf, $o + 0x2C)
        f30      = [BitConverter]::ToInt32($buf, $o + 0x30)
        f34      = [BitConverter]::ToInt32($buf, $o + 0x34)
        f38      = [BitConverter]::ToInt32($buf, $o + 0x38)
    }
}

$bad = $rows | Where-Object { $_.vt -ne ("0x{0:X}" -f $MAID_VT) }
if ($bad) { Write-Warning ("vtable 이 다른 칸이 " + $bad.Count + "개 있습니다 — 주소가 어긋났을 수 있습니다.") }

if ($Save) {
    $rows | Export-Csv -Path $Save -NoTypeInformation -Encoding UTF8
    Write-Host "스냅샷 저장: $Save ($MAID_COUNT 칸)"
    return
}

if ($Diff) {
    $old = Import-Csv -Path $Diff
    $changed = @()
    for ($i = 0; $i -lt $MAID_COUNT; $i++) {
        $a = $old[$i]; $b = $rows[$i]
        foreach ($f in 'intimacy','city','f28','f2c','f30','f34','f38') {
            if ([int]$a.$f -ne [int]$b.$f) {
                $changed += [pscustomobject]@{ row = $i; field = $f; before = [int]$a.$f; after = [int]$b.$f }
            }
        }
    }
    if ($changed) { $changed | Format-Table -AutoSize }
    else          { Write-Host "달라진 칸이 없습니다." }
    return
}

# 초기값: 친밀도 0 · f28 1 · f2c 0 · f30 1 · f34 0 · f38 -1 (새 게임 초기화 0x461E6C)
$view = if ($All) { $rows } else {
    $rows | Where-Object {
        $_.intimacy -ne 0 -or $_.f28 -ne 1 -or $_.f2c -ne 0 -or $_.f30 -ne 1 -or $_.f34 -ne 0 -or $_.f38 -ne -1
    }
}
if ($view) { $view | Format-Table -AutoSize }
else       { Write-Host "초기값과 다른 칸이 없습니다 (여급과 아직 아무 일도 없었습니다). -All 로 전부 볼 수 있습니다." }
