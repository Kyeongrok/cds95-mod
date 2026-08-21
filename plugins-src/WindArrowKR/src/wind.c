#include <windows.h>
#include "gameaddr.h"
#include "wind.h"

// 바다상태는 워드 두 개가 전부다. +0x00 바람, +0x02 해류.
// 한 워드의 속:  비트 0..3 방위 · 4..7 세기 · 8..11 기후대
static void Split(unsigned short w, Flow* out)
{
    if (!out) return;
    out->dir = w & 0xF;
    out->speed = (w >> 4) & 0xF;
}

void Wind_Now(Flow* wind, Flow* current)
{
    const unsigned short* state = (const unsigned short*)GA(GA_SEA_STATE - GA_BASE);
    Split(state[0], wind);
    Split(state[1], current);
}
