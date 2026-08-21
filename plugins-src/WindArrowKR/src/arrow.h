#pragma once

// 화살표 아틀라스 — 16방위 x 세기 3단 x (바람·해류) 두 벌 = 96장.
//
// 게임 그림이 아니라 여기서 만든다. 원본 항해 화면에는 화살표가 없다 — 흐름은 물결이
// 흐르고 구름이 지나가는 것으로만 보인다([[31.분석-바다 물결과 구름]]). 그러니 그림을
// 뽑아 올 데가 없다.
//
// 한 장은 8bpp 팔레트 색인이고 **색인 0 이 투명**이다. 게임 스프라이트(배 48x48, 색인 0
// 투명)와 규칙이 같아야 0x48AC30 이 그대로 찍어 준다.
//
// 방위는 표에 든 그대로 **불어가는 쪽**이다. 0 이 북(위)이고 반시계로 22.5도씩 돈다
// (벡터표 0x569558 과 같은 뜻). 화살촉이 그 방향으로 선다 — 기상학의 "북풍"과 반대이니
// 글자로 옮길 때만 조심하면 된다.

#define ARROW_W        24
#define ARROW_H        24
#define ARROW_DIRS     16
#define ARROW_LEVELS   3                 // 세기 약·중·강. 자루 길이로 보인다
#define ARROW_KINDS    2                 // 0 = 바람, 1 = 해류
#define ARROW_FRAMES   (ARROW_DIRS * ARROW_LEVELS * ARROW_KINDS)
#define ARROW_FRAME_SZ (ARROW_W * ARROW_H)
#define ARROW_ATLAS_SZ (ARROW_FRAME_SZ * ARROW_FRAMES)

// 프레임 번호. 세로로 쌓은 아틀라스의 몇 번째 장인가.
#define ARROW_FRAME(kind, level, dir)  (((kind) * ARROW_LEVELS + (level)) * ARROW_DIRS + (dir))

// 96장을 세로로 이어 붙인 그림(ARROW_W x ARROW_H*96). 한 번 만들어 두고 계속 쓴다.
// 실패하면 NULL.
const unsigned char* Arrow_Atlas(void);

// 세기를 0~2 단으로 깎는다. 흐름이 없는 칸(세기 0)이면 -1 이라 그리지 않는다.
int Arrow_Level(int speed);
