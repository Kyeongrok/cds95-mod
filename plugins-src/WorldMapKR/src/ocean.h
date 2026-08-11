#pragma once
#include <windows.h>

// OCEAN.CDS — 게임이 해상 화면을 그릴 때 쓰는 타일 그림.
//
// 게임 코드에서 확정했다(note: 옵시디안 "12.분석-WORLD.CDS 화면 그리기(런타임)"):
//   0x4898B0 이 WORLD.CDS 를 파일매핑으로 걸고, 이어서 OCEAN.CDS 를 4MB 통째로 읽는다.
//   0x48A1E0 이 칸을 읽어 `타일번호 = 칸 & 0x3FFF` 로 그림을 고른다.
//
// 파일은 Ls12 압축이고 파트가 둘이다.
//   파트0  원본 4,194,304 (0x400000) — 16x16 8bpp 타일 16,384장 (한 장 256바이트)
//   파트1  원본 258 — 정체 미상(색표라기엔 768/512 가 아니다). 안 쓴다.
//
// 색은 ShipSkinKR 이 해상 화면에서 역산해 둔 팔레트를 그대로 쓴다 — 타일이 그려지는
// 화면이 바로 그 화면이다. 도시 스프라이트 색까지 맞는 것으로 확인했다.

#define OCEAN_TILE_W    16
#define OCEAN_TILE_SZ   (OCEAN_TILE_W * OCEAN_TILE_W)   // 256
#define OCEAN_TILE_N    16384
#define OCEAN_DATA_SZ   (OCEAN_TILE_SZ * OCEAN_TILE_N)  // 0x400000
#define OCEAN_TILE_MASK 0x3FFF

// 게임 폴더의 OCEAN.CDS 를 풀어 올린다. 성공 1. 이미 올라와 있으면 그냥 1.
// 4MB 를 쓰므로 창을 닫을 때 Ocean_Free 로 놓아 준다.
int  Ocean_Load(void);
int  Ocean_Ready(void);
void Ocean_Free(void);

// 왜 못 올렸는지 한 줄. 올라왔으면 빈 문자열.
// 지도 창 아래에 그대로 띄운다 — DebugView 없이 원인을 보려고 둔 것이다.
const wchar_t* Ocean_Why(void);

// 타일 한 점의 색(0xRRGGBB). tile 0~16383, px/py 0~15.
unsigned Ocean_Pixel(int tile, int px, int py);
