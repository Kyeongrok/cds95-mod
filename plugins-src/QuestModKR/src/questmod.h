#pragma once
#include <windows.h>

// QuestModKR — 퀘스트 이벤트 파일(.CDS)을 통째로 갈아 끼우는 모드 전환기.
//
//   CDS95Util\mods\<모드이름>\      그 모드가 쓸 파일들
//        *.CDS                      게임 폴더로 복사할 퀘스트 파일 (ECQ EDG … PHT, STORY*)
//        quests.json                (선택) 그 모드 전용 편집 목록
//        mod.txt                    (선택) 첫 줄이 설명으로 뜬다
//   CDS95Util\questmod.state        지금 적용된 모드 이름 한 줄
//
// 게임이 뜰 때 state 를 읽어 그 모드가 그대로 깔려 있는지 보고, 아니면 다시 깐다.
// 이미 같은 것이 깔려 있으면 아무 것도 안 한다.
void QuestModKR_Init(HINSTANCE hinst);
