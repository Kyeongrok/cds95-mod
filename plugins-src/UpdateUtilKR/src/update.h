#pragma once
#include <windows.h>

// UpdateUtilKR — GitHub 릴리즈 목록을 받아 고른 판으로 갈아 끼운다.
//
// 왜 필요한가 — 새 판에 문제가 생겼을 때 옛 판으로 돌아갈 길이 없었다. 릴리즈가 33개나
// 쌓여 있으니 목록에서 골라 되돌릴 수 있게 한다. 기본은 맨 위(최신)다.
//
//   https://api.github.com/repos/Kyeongrok/cds95-mod/releases   목록
//   CDS95Util-<태그>.zip                                        받아서 푸는 것
//   CDS95Util\update.state                                      지금 깔린 태그
//
// 받아 온 .plugin 은 늘 덮어쓰고, .json 은 건드리지 않는다(사용자가 고쳐 쓰는 파일이라
// 덮으면 손댄 것이 날아간다). 새 .json 은 <이름>.new 로 옆에 놔 둔다.
// 쓰고 있는 .plugin 은 이름을 밀어내고 새것을 놓는다 — 반영은 늘 다음 실행부터.
void UpdateKR_Init(HINSTANCE hinst);
