# 발견물(274) -> 힌트(186) 매핑표를 만들어 disc_hint.h 로 굽는다.
#   이름: CDS_95.EXE 발견물 표(RVA 0x11C540, 92 x 274)
#   이음: cds-helper 발견물.json 의 hint 이름 -> hint.json(=hint_rows.h) 의 id
#   표기가 다른 것들은 아래 $Manual 로 손으로 이었다.
$ErrorActionPreference = "Stop"
$exePath = "$env:USERPROFILE\Desktop\대항해시대3\CDS_95.EXE"
$e = [IO.File]::ReadAllBytes($exePath)
$pe = [BitConverter]::ToInt32($e, 0x3C)
$nsec = [BitConverter]::ToUInt16($e, $pe + 6)
$optsz = [BitConverter]::ToUInt16($e, $pe + 20)
$sec = $pe + 24 + $optsz
$base = 0x400000
function RvaToOff($rva) {
  for ($i = 0; $i -lt $nsec; $i++) {
    $o = $sec + $i * 40
    $va = [BitConverter]::ToUInt32($e, $o + 12); $vs = [BitConverter]::ToUInt32($e, $o + 8); $raw = [BitConverter]::ToUInt32($e, $o + 20)
    if ($rva -ge $va -and $rva -lt $va + $vs) { return $raw + ($rva - $va) }
  }
  return -1
}
$tbl = RvaToOff 0x11C540
$names = @()
for ($i = 0; $i -lt 274; $i++) {
  $r = $tbl + $i * 92
  $no = RvaToOff ([BitConverter]::ToUInt32($e, $r) - $base)
  $j = $no; while ($e[$j] -ne 0) { $j++ }
  $names += [Text.Encoding]::GetEncoding(949).GetString($e, $no, $j - $no)
}

function Norm($s) { if ($null -eq $s) { return "" }; ($s -replace '[\s·・\.,]', '') }

$disc = Get-Content "C:\Users\ocean\git\cds-helper\CdsHelper\발견물.json" -Raw -Encoding UTF8 | ConvertFrom-Json
$hint = Get-Content "C:\Users\ocean\git\cds-helper\CdsHelper\hint.json" -Raw -Encoding UTF8 | ConvertFrom-Json
$hmap = @{}; foreach ($h in $hint) { $hmap[(Norm $h.name)] = [int]$h.id }
$hname = @{}; foreach ($h in $hint) { $hname[[int]$h.id] = $h.name }

# exe 이름 -> 인덱스 (같은 이름이 둘이면 먼저 것)
$nidx = @{}
for ($i = 0; $i -lt 274; $i++) { $k = Norm $names[$i]; if (-not $nidx.ContainsKey($k)) { $nidx[$k] = $i } }

# 발견물.json id -> 힌트 id. 이름 표기가 달라 자동으로 못 이은 것들.
$Manual = @{
  9 = 8; 19 = 19; 27 = 27; 36 = 34; 37 = 35; 39 = 36; 41 = 39; 42 = 152; 70 = 51; 78 = 58
  82 = 62; 83 = 63; 84 = 64; 86 = 66; 88 = 68; 90 = 70; 123 = 95; 130 = 101; 150 = 116
  152 = 118; 172 = 135; 174 = 136; 185 = 146; 187 = 148; 193 = 154; 211 = 165; 214 = 168; 225 = 179
  # json 에 hint 가 비어 있지만 어느 힌트인지 뻔한 것들
  13 = 13    # 파므카레(파묵칼레) = 흰 비늘계단
  89 = 69    # 푸에블로보닛 = 석조촌
  108 = 83   # 로제타석 = 고대 이집트의 기록
  114 = 88   # 시바신상 (이름이 그대로 있다)
  119 = 92   # 명나라의 칠보 = 중국의 칠보
  # json 은 배링해협에 "땅끝해협" 을 적어 뒀는데 그건 남쪽 이야기다. 북쪽해협이 맞다
  # (발견물 차례로도 남극대륙 다음이 배링해협, 힌트 차례로도 남극대륙 다음이 북쪽해협이다).
  12 = 11
}

$map = @(); for ($i = 0; $i -lt 274; $i++) { $map += -1 }
$src = @(); for ($i = 0; $i -lt 274; $i++) { $src += "" }
$warn = @()

foreach ($d in $disc) {
  # 발견물 표의 어느 줄인가 — 이름이 맞으면 그 줄, 아니면 id-1 로 본다.
  $idx = -1
  $k = Norm $d.name
  if ($nidx.ContainsKey($k)) { $idx = $nidx[$k] }
  elseif ($d.id -ge 1 -and $d.id -le 274) { $idx = $d.id - 1; $warn += "이름 못 찾음: json $($d.id) '$($d.name)' -> exe[$idx] '$($names[$idx])'" }
  if ($idx -lt 0) { continue }

  $hid = -1; $how = ""
  if ($Manual.ContainsKey([int]$d.id)) { $hid = $Manual[[int]$d.id]; $how = "manual" }
  elseif ($d.hint) {
    $hk = Norm $d.hint
    if ($hmap.ContainsKey($hk)) { $hid = $hmap[$hk]; $how = "auto" }
    else { foreach ($part in ($d.hint -split ',')) { $p = Norm $part; if ($hmap.ContainsKey($p)) { $hid = $hmap[$p]; $how = "auto/split"; break } } }
  }
  if ($hid -lt 0) { continue }
  if ($map[$idx] -ge 0 -and $map[$idx] -ne $hid) { $warn += "겹침: exe[$idx] '$($names[$idx])' 에 힌트 $($map[$idx]) 와 $hid 가 둘 다" }
  $map[$idx] = $hid
  $src[$idx] = $how
}

$linked = ($map | Where-Object { $_ -ge 0 }).Count
"이은 발견물: $linked / 274"
$usedH = ($map | Where-Object { $_ -ge 0 } | Sort-Object -Unique).Count
"쓰인 힌트: $usedH / 186"
if ($warn.Count) { "경고:"; $warn | ForEach-Object { "  $_" } }
"못 이은 힌트(대응 발견물 없음):"
$used = @{}; foreach ($m in $map) { if ($m -ge 0) { $used[$m] = 1 } }
(0..185 | Where-Object { -not $used.ContainsKey($_) } | ForEach-Object { "$_ $($hname[$_])" }) -join ' | '

# ---- 헤더 굽기 ----
$sb = New-Object Text.StringBuilder
[void]$sb.AppendLine('#pragma once')
[void]$sb.AppendLine('// 발견물(274) -> 힌트(186) 이음표. 값이 -1 이면 그 발견물에는 힌트가 없다.')
[void]$sb.AppendLine('//')
[void]$sb.AppendLine('// 왜 필요한가 — 게임이 "발견했는가"를 들고 있는 자리는 힌트 배열(186칸, hintdb.h)뿐이다.')
[void]$sb.AppendLine('// 발견물 표 274개는 교역품·비보까지 담은 이름표라 상태 칸이 아예 없다. 그래서 발견물 줄의')
[void]$sb.AppendLine('// 발견 여부는 이 표로 힌트 번호를 찾아 그 상태를 읽어 온다(세이브가 아니라 실행 중 값이다).')
[void]$sb.AppendLine('//')
[void]$sb.AppendLine('// 만든 법: cds-helper 발견물.json 의 hint 이름을 hint.json(=hint_rows.h) 이름과 맞췄다.')
[void]$sb.AppendLine('// 표기가 어긋난 28개(지팡크->지팡그, 인더스의 요새->인더스의 성체, 촉수초->더듬이풀 …)는')
[void]$sb.AppendLine('// 손으로 이었다. tools/gen-disc-hint.ps1 을 다시 돌리면 이 파일을 새로 굽는다.')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('#define DISC_HINT_N 274')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('static const short kDiscHint[DISC_HINT_N] = {')
for ($i = 0; $i -lt 274; $i++) {
  $c = if ($map[$i] -ge 0) { "$($names[$i]) -> $($hname[$map[$i]])" } else { "$($names[$i])" }
  [void]$sb.AppendLine(("    {0,4},  // {1} {2}" -f $map[$i], $i, $c))
}
[void]$sb.AppendLine('};')
$out = "C:\Users\ocean\git\wpf\cds95-mod\plugins-src\HintUtilKR\src\disc_hint.h"
[IO.File]::WriteAllText($out, $sb.ToString(), (New-Object Text.UTF8Encoding $false))
"wrote $out"
