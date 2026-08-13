#pragma once

// 24bpp 그림을 도시 그림의 색인 그림으로 되돌린다.
//
// 도시 그림 한 장은 색을 두 군데서 가져온다(citycg.h 참고).
//   색인 10~73   게임 공용 색표 — 모든 그림이 나눠 쓰는 고정색
//   색인 74~159  그 그림에만 딸린 86색 팔레트 — 우리가 새로 짜 넣을 수 있는 자리
// 그래서 넣을 때도 둘을 같이 후보로 둔다. 고정색 64개는 공짜로 쓰고, 나머지 색만
// 새 팔레트 86색으로 채우면 실제로 쓸 수 있는 색이 150개가 되어 원본과 조건이 같아진다.
//
// 색이 얼마 없는 그림(내보낸 PNG 를 고쳐 온 경우가 대개 그렇다)은 쓰인 색을 그대로
// 팔레트에 담아 한 점도 안 틀리게 되돌린다. 색이 많으면 미디언컷으로 86색을 고른다.

// rgb(npix * 3, R·G·B 순)를 색인 그림으로 바꾼다.
//   gamePal  게임 공용 색표 768바이트(R·G·B 순)
//   fixLo/fixHi  그 중 고정색으로 쓸 색인 범위(양끝 포함)
//   palBase/palN 새 팔레트가 앉을 첫 색인과 색 수
//   palOut   만들어진 팔레트 palN*3 바이트(R·G·B 순)
//   idxOut   색인 그림 npix 바이트
// 쓰인 색이 palN 이하로 딱 떨어져 한 점도 안 틀렸으면 1, 근사했으면 0 을 돌려준다.
int Quant_Index(const unsigned char* rgb, int npix,
                const unsigned char* gamePal, int fixLo, int fixHi,
                int palBase, int palN,
                unsigned char* palOut, unsigned char* idxOut);
