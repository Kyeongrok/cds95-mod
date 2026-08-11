#include "sprite.h"

// 자리(모듈 + RVA). 규칙은 sprite.h 에 적어 뒀다.
#define RVA_ATLAS_SEA   0x001D68C8u   // 배 4벌 x 8방향
#define RVA_ATLAS_LAND  0x002092D0u   // 말(대상) — 육상·정박
#define RVA_DOCKED      0x001B61B4u   // 0 이면 항해 중
#define RVA_HEADING     0x001B63C8u   // 16방향
#define RVA_CLASS_TAB   0x001695D8u   // 함선종류 -> 그림 클래스
#define RVA_LAND_BASE   0x00169550u   // 말 그림의 밑번호
#define RVA_FLEETOBJ    0x001B3928u   // 함대 객체(기함을 여기서 잡는다)

#define SEA_FRAMES   32
#define LAND_FRAMES  32

static unsigned char* Base(void)
{
    static unsigned char* b = NULL;
    if (!b) b = (unsigned char*)GetModuleHandleW(NULL);
    return b;
}

static int Readable(const void* p, unsigned n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const unsigned char*)p + n <= (unsigned char*)mbi.BaseAddress + mbi.RegionSize;
}

static int I32(unsigned rva, int* out)
{
    const int* p;
    if (!Base()) return 0;
    p = (const int*)(Base() + rva);
    if (!Readable(p, 4)) return 0;
    *out = *p;
    return 1;
}

// 기함의 함선종류. 못 잡으면 -1.
// 게임이 0x48A837 에서 하는 그대로다 — 함대 객체에서 기함 슬롯을 얻고(0x473CD0),
// 그 슬롯의 배를 잡아(0x473DC0) 종류 getter(0x44C6E0) 를 부른다.
// 여기서는 게임 함수를 부르지 않고 자리에서 바로 읽는다 — 그리는 도중에 게임 코드를
// 부르면 상태가 꼬일 수 있어서다. 함대 목록은 marketdb 에서 확정한 자리와 같다.
static int FlagshipType(void)
{
    const int* slot;
    const unsigned char* ship;
    int id;
    if (!Base()) return -1;
    slot = (const int*)(Base() + RVA_FLEETOBJ + 4);   // 함대 여덟 칸의 첫 칸
    if (!Readable(slot, 4)) return -1;
    id = *slot;
    if (id < 0 || id >= 16) return -1;
    ship = Base() + 0x1A4E18u + (unsigned)id * 0x6Cu;  // 배 struct
    if (!Readable(ship, 0x6C)) return -1;
    return *(const int*)(ship + 0x28);                 // 함선종류
}

const unsigned char* Sprite_Now(void)
{
    int docked = 0, heading = 0, frame, cls, type, landBase = 0;
    const unsigned char* atlas;
    int nframes;

    if (!Base()) return NULL;
    if (!I32(RVA_DOCKED, &docked)) return NULL;
    if (!I32(RVA_HEADING, &heading)) return NULL;

    if (docked == 0) {
        // 항해 중 — 배
        const int* tab;
        type = FlagshipType();
        cls = 0;
        if (type >= 0 && type < 16) {
            tab = (const int*)(Base() + RVA_CLASS_TAB + (unsigned)type * 4);
            if (Readable(tab, 4) && *tab >= 0 && *tab < 4) cls = *tab;
        }
        frame = cls * 8 + (heading >> 1);      // 16방향 -> 8방향
        atlas = Base() + RVA_ATLAS_SEA;
        nframes = SEA_FRAMES;
    } else {
        // 육상·정박 — 말
        int d = heading + 1;
        if (d < 0) d = -d;
        d &= 0xF;
        I32(RVA_LAND_BASE, &landBase);
        frame = (d >> 2) * 8 + landBase;
        atlas = Base() + RVA_ATLAS_LAND;
        nframes = LAND_FRAMES;
    }

    if (frame < 0 || frame >= nframes) frame = 0;
    if (!Readable(atlas + (unsigned)frame * SPR_SZ, SPR_SZ)) return NULL;
    return atlas + (unsigned)frame * SPR_SZ;
}
