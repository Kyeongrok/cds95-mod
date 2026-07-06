<#
.SYNOPSIS
  해상 배 스프라이트 아틀라스(0x5D68C8, 4슬롯×8방향×48×48 팔레트인덱스) 추출/패킹 도구 (sp5-7).

.DESCRIPTION
  ShipSkinKR의 Decode(0x463680) 훅이 %TEMP%\cds_ship_atlas.bin (정확히 0x12000B) 이 있으면
  해상 배 아틀라스를 그 픽셀로 교체한다(스탯 불변, 이미지만). 이 스크립트는:
    dump  : 실행중 게임 메모리 0x5D68C8 에서 현재 아틀라스를 읽어 raw .bin + 흑백 컨택트시트 PNG 로 저장
    pack  : 편집한 .bin(또는 흑백 PNG 시트)을 %TEMP%\cds_ship_atlas.bin 으로 설치(재시작 후 반영)
    clear : 오버레이 파일 제거(원본 복귀)
  레이아웃: 아틀라스 = gid 0..3 (코구/다우, 카라벨, 카락, 갤리온), 각 gid = 방향 0..7, 각 프레임 48×48(2304B).
  offset = (gid*8 + dir)*2304. 인덱스 0 = 투명.

.EXAMPLE
  .\cds-ship-atlas.ps1 dump                 # %TEMP%\cds_ship\ 에 atlas.bin + sheet_gray.png
  .\cds-ship-atlas.ps1 pack -In .\atlas.bin # 편집본 설치 → 게임 재시작 후 반영
  .\cds-ship-atlas.ps1 clear
#>
param(
  [Parameter(Position=0)][ValidateSet('dump','pack','clear')][string]$Cmd = 'dump',
  [string]$In,
  [string]$Out = (Join-Path $env:TEMP 'cds_ship'),
  [int]$Scale = 4
)
$ErrorActionPreference = 'Stop'

$ATLAS = 0x5D68C8
$LEN   = 0x12000           # 4 * 8 * 2304
$FRAME = 2304; $FW = 48; $FH = 48; $NGID = 4; $NDIR = 8
$OVERLAY = Join-Path $env:TEMP 'cds_ship_atlas.bin'

Add-Type -AssemblyName System.Drawing

function Get-RM {
  Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;
public class ShipRM{
 [DllImport("kernel32.dll")]public static extern IntPtr OpenProcess(uint a,bool i,int p);
 [DllImport("kernel32.dll")]public static extern bool ReadProcessMemory(IntPtr h,IntPtr a,byte[] b,int s,out int r);
 [DllImport("kernel32.dll")]public static extern bool CloseHandle(IntPtr h);}
"@ -ErrorAction SilentlyContinue
}

function Read-Atlas {
  Get-RM
  $p = Get-Process -Name cds_95 -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $p) { throw "cds_95 미실행 — 게임을 켜고(해상도창 이후면 충분) 다시 실행하세요." }
  $h = [ShipRM]::OpenProcess(0x10, $false, $p.Id)
  try {
    $b = New-Object byte[] $LEN; $r = 0
    [void][ShipRM]::ReadProcessMemory($h, [IntPtr]$ATLAS, $b, $LEN, [ref]$r)
    if ($r -ne $LEN) { throw "메모리 읽기 실패(read=$r)" }
    return $b
  } finally { [void][ShipRM]::CloseHandle($h) }
}

# 인덱스 → 표시색: 0=투명(마젠타 표시), 그 외 = 흑백(값=명도). 편집/식별용.
function Index-ToGray([byte]$idx) {
  if ($idx -eq 0) { return ,@(255,0,255) }   # 투명 = 마젠타(눈에 띔)
  return ,@($idx,$idx,$idx)
}

function Cmd-Dump {
  $data = Read-Atlas
  New-Item -ItemType Directory -Force -Path $Out | Out-Null
  $binPath = Join-Path $Out 'atlas.bin'
  [System.IO.File]::WriteAllBytes($binPath, $data)

  # 컨택트시트: 세로 gid(4행) × 가로 dir(8열), 각 프레임 48×48 × Scale 확대. 프레임 사이 1px 격자.
  $cellW = $FW * $Scale; $cellH = $FH * $Scale; $gap = 1
  $sheetW = $NDIR * $cellW + ($NDIR + 1) * $gap
  $sheetH = $NGID * $cellH + ($NGID + 1) * $gap
  $bmp = New-Object System.Drawing.Bitmap($sheetW, $sheetH)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::FromArgb(40,40,40))
  for ($gid = 0; $gid -lt $NGID; $gid++) {
    for ($dir = 0; $dir -lt $NDIR; $dir++) {
      $base = ($gid * $NDIR + $dir) * $FRAME
      $ox = $gap + $dir * ($cellW + $gap)
      $oy = $gap + $gid * ($cellH + $gap)
      for ($y = 0; $y -lt $FH; $y++) {
        for ($x = 0; $x -lt $FW; $x++) {
          $idx = $data[$base + $y * $FW + $x]
          $rgb = Index-ToGray $idx
          $c = [System.Drawing.Color]::FromArgb($rgb[0], $rgb[1], $rgb[2])
          $br = New-Object System.Drawing.SolidBrush($c)
          $g.FillRectangle($br, $ox + $x*$Scale, $oy + $y*$Scale, $Scale, $Scale)
          $br.Dispose()
        }
      }
    }
  }
  $g.Dispose()
  $sheetPath = Join-Path $Out 'sheet_gray.png'
  $bmp.Save($sheetPath, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()

  # 프레임별 원본 raw 도 개별 저장(정밀 편집용): g{gid}_d{dir}.bin (2304B)
  $fdir = Join-Path $Out 'frames'
  New-Item -ItemType Directory -Force -Path $fdir | Out-Null
  for ($gid = 0; $gid -lt $NGID; $gid++) {
    for ($dir = 0; $dir -lt $NDIR; $dir++) {
      $base = ($gid * $NDIR + $dir) * $FRAME
      $fb = New-Object byte[] $FRAME
      [Array]::Copy($data, $base, $fb, 0, $FRAME)
      [System.IO.File]::WriteAllBytes((Join-Path $fdir ("g{0}_d{1}.bin" -f $gid,$dir)), $fb)
    }
  }

  # 인덱스 사용 통계
  $used = @{}
  foreach ($v in $data) { if ($v -ne 0) { $used[$v] = ($used[$v] + 1) } }
  "덤프 완료:"
  "  raw   : $binPath  ($LEN bytes)"
  "  sheet : $sheetPath  (${sheetW}x${sheetH}, gid=행/dir=열, 마젠타=투명)"
  "  frames: $fdir\g{gid}_d{dir}.bin  (32개 × $FRAME B)"
  "  사용 인덱스 종류(0 제외) = $($used.Count)"
}

function Cmd-Pack {
  if (-not $In) { throw "사용법: pack -In <atlas.bin 또는 sheet_gray.png>" }
  if (-not (Test-Path $In)) { throw "입력 없음: $In" }
  $ext = [System.IO.Path]::GetExtension($In).ToLower()
  if ($ext -eq '.bin') {
    $data = [System.IO.File]::ReadAllBytes($In)
    if ($data.Length -ne $LEN) { throw "크기 불일치: $($data.Length) (기대 $LEN)" }
  }
  elseif ($ext -eq '.png') {
    # 흑백 시트 역변환: 프레임 셀에서 픽셀 = 인덱스(R채널). 마젠타(255,0,255)=투명 0.
    $bmp = [System.Drawing.Image]::FromFile($In)
    $cellW = $FW * $Scale; $cellH = $FH * $Scale; $gap = 1
    $data = New-Object byte[] $LEN
    for ($gid = 0; $gid -lt $NGID; $gid++) {
      for ($dir = 0; $dir -lt $NDIR; $dir++) {
        $base = ($gid * $NDIR + $dir) * $FRAME
        $ox = $gap + $dir * ($cellW + $gap)
        $oy = $gap + $gid * ($cellH + $gap)
        for ($y = 0; $y -lt $FH; $y++) {
          for ($x = 0; $x -lt $FW; $x++) {
            $px = $bmp.GetPixel($ox + $x*$Scale, $oy + $y*$Scale)
            if ($px.R -eq 255 -and $px.G -eq 0 -and $px.B -eq 255) { $idx = 0 }
            else { $idx = $px.R }
            $data[$base + $y*$FW + $x] = [byte]$idx
          }
        }
      }
    }
    $bmp.Dispose()
  }
  else { throw "지원 형식: .bin 또는 .png (흑백 시트)" }
  [System.IO.File]::WriteAllBytes($OVERLAY, $data)
  "설치 완료: $OVERLAY ($LEN bytes)"
  "→ 게임 재시작하면 해상 배가 이 아틀라스로 교체됩니다. (원복: clear)"
}

function Cmd-Clear {
  if (Test-Path $OVERLAY) { Remove-Item $OVERLAY -Force; "제거됨: $OVERLAY (재시작 후 원본 배)" }
  else { "이미 없음: $OVERLAY" }
}

switch ($Cmd) {
  'dump'  { Cmd-Dump }
  'pack'  { Cmd-Pack }
  'clear' { Cmd-Clear }
}
