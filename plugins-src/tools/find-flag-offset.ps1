# 도시 구조체(base=0x005863B4=시세, stride=92) 안에서, 알려진 "조합 유무" 패턴과
# 일치하는 바이트/비트 오프셋을 찾는다.
$ErrorActionPreference = "Stop"
if(-not ("MG" -as [type])){ Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class MG {
  [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint a, bool i, int p);
  [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] b, int s, out int r);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
}
"@ }
$proc = Get-Process -Name cds_95 -ErrorAction SilentlyContinue | Select-Object -First 1
if(-not $proc){ Write-Host "게임 없음"; exit 1 }
$H = [MG]::OpenProcess(0x0410,$false,$proc.Id)
function RdMem([long]$a,[int]$n){ $b=New-Object byte[] $n; $r=0; [void][MG]::ReadProcessMemory($H,[IntPtr]$a,$b,$n,[ref]$r); if($r -eq $n){return ,$b}; return $null }

$SISE = 0x005863B4L
$STRIDE = 92
$N = 226

# 알려진 정답: index -> 조합 유무
$known = @{ 0=$true; 1=$false; 2=$false; 7=$true; 9=$true; 84=$false }

# 전체 도시의 각 오프셋 바이트를 미리 읽어둔다: cityByte[k][off]
# 구조체 범위를 넉넉히 (시세 기준 delta -48..+48)
$dLo = -48; $dHi = 48
$vals = @{}   # key "k_d" -> byte
for($k=0;$k -lt $N;$k++){
    $rowAddr = $SISE + $k*$STRIDE + $dLo
    $len = $dHi - $dLo + 1
    $buf = RdMem $rowAddr $len
    for($d=$dLo;$d -le $dHi;$d++){ $vals["$k`_$d"] = if($buf){$buf[$d-$dLo]}else{0} }
}

function CountNonZero($d,[int]$bit){
    $c=0; for($k=0;$k -lt $N;$k++){ $b=$vals["$k`_$d"]; $on= if($bit -lt 0){$b -ne 0}else{(($b -shr $bit) -band 1) -eq 1}; if($on){$c++} }
    return $c
}

Write-Host "=== 바이트 모드 (nonzero=조합있음) ===" -ForegroundColor Cyan
$hits = @()
for($d=$dLo;$d -le $dHi;$d++){
    $ok=$true
    foreach($idx in $known.Keys){ $b=$vals["$idx`_$d"]; $on=($b -ne 0); if($on -ne $known[$idx]){$ok=$false;break} }
    if($ok){ $cnt=CountNonZero $d -1; $hits += [PSCustomObject]@{Mode="byte";Delta=$d;Addr=($SISE+$d);Count=$cnt} }
}
Write-Host "=== 비트 모드 (bit set=조합있음) ===" -ForegroundColor Cyan
for($d=$dLo;$d -le $dHi;$d++){
    for($bit=0;$bit -lt 8;$bit++){
        $ok=$true
        foreach($idx in $known.Keys){ $b=$vals["$idx`_$d"]; $on=((($b -shr $bit) -band 1) -eq 1); if($on -ne $known[$idx]){$ok=$false;break} }
        if($ok){ $cnt=CountNonZero $d $bit; $hits += [PSCustomObject]@{Mode="bit$bit";Delta=$d;Addr=($SISE+$d);Count=$cnt} }
    }
}

Write-Host ("`n일치 후보: {0}개 (알려진 6도시 패턴 만족)" -f $hits.Count) -ForegroundColor Green
foreach($h in $hits){
    Write-Host ("`n[{0}] delta={1,3}  addr(city0)=0x{2:X8}  조합보유도시수={3}/226" -f $h.Mode,$h.Delta,$h.Addr,$h.Count) -ForegroundColor Yellow
    # 도시 0~24 의 유무 벡터 미리보기
    $line="  0~24: "
    for($k=0;$k -lt 25;$k++){
        $b=$vals["$k`_$($h.Delta)"]
        $on= if($h.Mode -eq "byte"){$b -ne 0}else{ $bit=[int]($h.Mode.Substring(3)); (($b -shr $bit) -band 1) -eq 1 }
        $line += if($on){"O"}else{"."}
    }
    Write-Host $line -ForegroundColor Gray
}
Write-Host "`n(O=조합있음). 알려진 도시: 0리스본O 1오포르토. 2라코루냐. 7세빌리아O 9말라가O 84세우타." -ForegroundColor DarkGray
[void][MG]::CloseHandle($H)
