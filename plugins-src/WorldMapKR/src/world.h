#pragma once
#include <windows.h>

// WORLD.CDS(게임 폴더) 를 읽어 세계지도를 그린다.
//
// 파일 배치 (cds-helper 의 WorldMapRenderer 와 같다. 6,250,000 바이트로 크기까지 맞는다):
//   2500 바이트 x 2500 행. 한 칸이 2바이트 — [0]=지형(하위 7비트), [1]=속성.
//   가로는 1250칸이고, 짝수 행이 지도의 왼쪽 절반, 홀수 행이 오른쪽 절반이다.
//   그래서 펼치면 2500 x 1250 칸이 되고 그게 경도 -180~180 / 위도 90~-90 에 그대로 대응한다.
//
// 지형 0 = 바다(속성 0 이면 해안선), 1 = 육지, 그 밖은 해안 칸이라 바다색과 육지색을
// 지형별 비율로 섞는다. 색과 비율표는 cds-helper 의 기본 팔레트를 그대로 옮겼다.

#define WORLD_RAW_STRIDE 2500
#define WORLD_CELL_W     1250
#define WORLD_CELL_H     1250
#define WORLD_UNFOLD_W   2500                                   // 펼친 폭
#define WORLD_FILE_SIZE  (WORLD_RAW_STRIDE * WORLD_CELL_H * 2)  // 6,250,000

// 게임 실행 파일과 같은 폴더의 WORLD.CDS 를 통째로 메모리에 올린다. 성공 1.
// 줌/이동할 때마다 다시 그려야 해서 원본을 들고 있는다(6.25MB, 창을 닫으면 놓아준다).
int  World_Load(void);
int  World_Loaded(void);
void World_Free(void);

// 셀 격자의 [x0, y0, vw x vh] 영역을 w x h 픽셀로 그린다. 성공 1.
// 화면 크기로 바로 줄여 그리므로 줌 배율과 상관없이 그리는 비용이 같다.
int World_RenderView(int w, int h, int x0, int y0, int vw, int vh);

int World_W(void);
int World_H(void);
// 24bpp BGR, 위에서 아래로. SetDIBitsToDevice 에 그대로 넣으면 된다.
const unsigned char* World_Pixels(void);
