#include <windows.h>
#include <math.h>
#include "arrow.h"

// 해상 팔레트에서 고른 색인이다. 표는 WorldMapKR/src/ocean_palette.h 에 구워져 있다.
//   10  = (255,255,255) 흰색      — 바람 몸통
//   189 = (190,200,50)  밝은 연두 — 해류 몸통 (바다색 123=(80,110,129) 위에서 제일 잘 뜬다)
//   50  = ( 27, 46, 82) 짙은 남색 — 둘 다 테두리
// 색인만 쓰므로 팔레트가 갈려도 우리가 할 일은 없다 — 게임이 그때그때 올린 색으로 나온다.
#define IDX_WIND_BODY     10
#define IDX_CURRENT_BODY  189
#define IDX_EDGE          50

// 화살표는 위(-y)를 보는 모양으로 그려 놓고 방위만큼 돌린다. 원점은 칸 가운데다.
#define HALF   (ARROW_W / 2.0f)
#define TIP_Y  (-9.0f)      // 화살촉 끝
#define NECK_Y (-3.0f)      // 촉과 자루가 만나는 곳
#define BARB_X  5.0f        // 촉 날개가 벌어지는 폭
#define BODY    1.6f        // 자루 굵기(반)
#define EDGE    1.2f        // 테두리 두께

// 세기 3단의 자루 끝. 길수록 센 흐름이다.
static const float kTailY[ARROW_LEVELS] = { 3.0f, 6.5f, 10.0f };

static unsigned char* g_atlas;

int Arrow_Level(int speed)
{
    if (speed <= 0) return -1;      // 무풍·무해류 칸은 아예 안 그린다
    if (speed <= 2) return 0;
    if (speed <= 4) return 1;
    return 2;
}

// 점에서 선분까지의 거리.
static float Segment(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float len2 = vx * vx + vy * vy;
    float t = len2 <= 0.0f ? 0.0f : (wx * vx + wy * vy) / len2;
    float dx, dy;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    dx = wx - t * vx;
    dy = wy - t * vy;
    return (float)sqrt(dx * dx + dy * dy);
}

// 화살표 뼈대(자루 하나 + 날개 둘)까지의 거리.
static float Distance(float x, float y, float tailY)
{
    float d = Segment(x, y, 0.0f, tailY, 0.0f, TIP_Y);
    float b = Segment(x, y, 0.0f, TIP_Y, -BARB_X, NECK_Y);
    float c = Segment(x, y, 0.0f, TIP_Y, BARB_X, NECK_Y);
    if (b < d) d = b;
    if (c < d) d = c;
    return d;
}

static void BuildFrame(unsigned char* dst, int dir, float tailY, unsigned char body)
{
    // 방위는 반시계로 22.5도씩, 0 이 북(위)이다. 방위 0 -> (0,-1).
    double a = dir * (2.0 * 3.14159265358979 / ARROW_DIRS);
    float ux = (float)(-sin(a));
    float uy = (float)(-cos(a));
    int x, y;

    for (y = 0; y < ARROW_H; y++)
        for (x = 0; x < ARROW_W; x++) {
            float px = x + 0.5f - HALF;
            float py = y + 0.5f - HALF;
            // 화면 점을 화살표 제 좌표로 되돌린다(돌림표의 전치).
            float lx = -uy * px + ux * py;
            float ly = -ux * px - uy * py;
            float d = Distance(lx, ly, tailY);
            unsigned char v = 0;
            if (d <= BODY) v = body;
            else if (d <= BODY + EDGE) v = IDX_EDGE;
            dst[y * ARROW_W + x] = v;
        }
}

const unsigned char* Arrow_Atlas(void)
{
    int kind, level, dir;

    if (g_atlas) return g_atlas;
    g_atlas = (unsigned char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ARROW_ATLAS_SZ);
    if (!g_atlas) return NULL;

    for (kind = 0; kind < ARROW_KINDS; kind++)
        for (level = 0; level < ARROW_LEVELS; level++)
            for (dir = 0; dir < ARROW_DIRS; dir++) {
                int frame = ARROW_FRAME(kind, level, dir);
                BuildFrame(g_atlas + frame * ARROW_FRAME_SZ, dir, kTailY[level],
                           kind == 0 ? IDX_WIND_BODY : IDX_CURRENT_BODY);
            }
    return g_atlas;
}
