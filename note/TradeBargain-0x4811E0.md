# 교역소 흥정(값 깎기) 역분석

대상: **한국어판 `cds_95.exe`** (Ver.1.2.0.0, MD5 `c2b292460f4c4fb582a78909efd2c047`,
경로 `대항해시대3/CDS_95.EXE`). ImageBase 0x400000.
`.text` 0x401000, `.rdata` 0x4C3000, `.data` 0x52F000.

> 일본판 `cds95runfilejp/CDS95.EXE`(1.4.0.0)는 주소가 전혀 다르다. 아래는 전부 한국어판 기준.
> [[cds95-live-addresses-differential-scan]]

찾은 길: `.data`에서 메뉴 문자열 "값을 깎는다"(CP949 `B0AA C0BB 20 B1F0 B4C2 B4D9`)를 찾아
그 VA를 `.text` 전체에서 4바이트 리터럴로 역참조 검색했다. 참조가 딱 한 군데였다.

| 문자열 | VA | 참조 |
|---|---|---|
| "결정" | 0x532888 | 0x481209 |
| "값을 깎는다" | 0x532890 | 0x481211 |
| "돌아간다" | 0x5328A0 | 0x481219 |

## 함수 0x004811E0 = 흥정 메뉴 (BargainMenu)

```c
// __thiscall, ret 8 (인자 2개)
int BargainMenu(Ctx* ecx, int* price, int* outMode);
```

- `Ctx` = 교역 대화 컨텍스트. 판매목록 빌더가 쓰는 그 객체다
  (`note/TradeSaleListBuilder-0x480CC0.md`의 `Ctx`).
  - `Ctx+0x80` = 도시 표시명(대사 출력에 넘긴다)
  - `Ctx+0xBC` = **이번 거래의 흥정 시도 횟수**. 대화를 열 때 0.
- `price`는 **in/out**이다 — 깎이면 이 자리 값이 바로 줄어든다.
- 반환 1 = 호출자가 `outMode`대로 처리하고 계속, 0 = 그냥 구입창으로 돌아가라.

### 게이트 — 메뉴가 아예 안 뜨는 조건

```
0x4811EC  push [price] ; call 0x4A17F0(ecx = this) ; call 0x481400
0x4811FF  je 0x48130F      ; 0 이면 outMode=0(=그냥 결정) 으로 빠진다
```

```c
// 0x004A17F0 __thiscall int BargainSkill(Ctx*)   — 인자 없음, ecx 만 쓴다
//   0x4A17C0: [ecx+0x90](도시 id) -> 0x429970(도시 객체)
//   0x429DA0: 그 객체 -> 0x429D40 -> 0x41B420 -> [0x4CA374 + n*24]
//   = 그 도시 문화권이 흥정에 보는 **재주 번호**. 도시마다 다르다.

// 0x00481400 __cdecl
int CanBargain(int skill, int price) {
    return price > 0 && BestOfFleet(skill, 0) >= 2;   // 0x468FE0
}

// 0x00468FE0 __cdecl int BestOfFleet(int skill, int flag)
//   skill == -1 이면 0. 주인공(0x5B60A0 의 가상함수 +0x20)과 동승 인물들(0x47CC60 으로
//   자리 0 · 3 …을 훑는다) 중 그 재주의 **최고치**를 돌려준다.
```

즉 **가격이 0 초과이고, 그 도시가 보는 재주가 함대 통틀어 2 이상**이어야 메뉴가 뜬다.
초반 주인공은 [결정]을 눌러도 메뉴 없이 바로 사진다.

> 처음엔 0x4A17F0 을 `Player()`로, 0x481400 의 첫 인자를 캐릭터 포인터로 읽었다. 틀렸다 —
> 0x4A17F0 은 ecx(교역 대화 객체)를 받는 `__thiscall`이고 돌려주는 것은 **번호**다.

### 메뉴

0x481205~0x481245가 스택에 **12바이트 x 3칸** 배열을 세운다(`{문자열, 1, 1}`) —
`0x00469A70(배열, 3, 1, 0, 0)`(cdecl, `add esp,0x14`)가 그 메뉴를 띄우고 **0-based 인덱스**를
돌려준다.

| 반환 | 항목 | 처리 |
|---|---|---|
| 0 | 결정 | `outMode = 0`, return 1 |
| 1 | 값을 깎는다 | 아래 흥정 처리 |
| 2 | 돌아간다 | `outMode = 1`, **return 0** |

(반환 매핑은 호출자 0x481580 쪽 분기와 맞춰 확정했다 — 아래 "호출자" 참고.)

### 흥정 처리 (0x481270~)

```c
Sound(1, 1);                                  // 0x4697C0
int ok = BargainRoll();                       // 0x481330
if (ok) { int p = *price * 90 / 100; if (p <= 1) p = 1; *price = p; }
BargainLine(Ctx->name80, ok, Ctx->tries, *price);   // 0x481380 — 대사
int n = ++Ctx->tries;                         // +0xBC
if (ok) { if (n >= 3) { *outMode = 0; Sound(1, 3); } else *outMode = 1; }
else    { if (n >= 2) *outMode = 2;                  else *outMode = 1; }
return 1;
```

- **한 번 깎을 때마다 90%**, 최소 1닢. 여러 번 성공하면 곱해서 깎인다.
- **성공 3번째**면 더 못 깎고 그대로 거래 성립(`outMode=0`).
- **실패 2번째**면 결렬(`outMode=2`) — 호출자가 벌칙을 준다.
- 첫 실패(또는 성공 1·2회)는 `outMode=1`이라 메뉴가 다시 뜬다.

### 성공 판정 0x00481330 (`__cdecl int BargainRoll(void)`)

```c
int lv = *(int*)0x5B6104;                      // 주인공 쪽 값
void* mate = Sub_47CC60((void*)0x5B60A0, 0, 0);   // 동승 인물
if (mate) { int v = Ability(mate, 9); if (v > lv) lv = v; }   // 0x468F40(mate, 9)
if (lv > 3) lv = 3;  if (lv < 0) lv = 0;
return Rand100Less(*(int*)(0x569330 + lv*4));  // 0x4B7C62 = (rand()%100 < p)
```

확률표 **0x569330 = {40, 70, 85, 95}** (%). 주인공과 동승자 중 **높은 쪽**을 쓴다.

### 대사 0x00481380 (`__cdecl void BargainLine(void* name, int ok, int tries, int price)`)

`tries`(0~2로 클램프)와 성공 여부로 `.data` 문자열을 골라 `0x4692E0`(대사 출력)에 넘긴다.

| tries | 성공 | 실패 |
|---|---|---|
| 0 | 0x532770 "으음, 그래 좋소. 금화 %ld닢에 타협해 봅시다." | 0x532800 "무리한 얘기다. 이 가격에 봐 주게." |
| 1 | 0x5327A0 "어쩔 수 없군. 금화 %ld닢에 어떤가?" | 0x532828 "이쪽은 바쁘다네. 살 마음이 없으면 돌아가게." |
| 2 | 0x5327C8 "카-앗. 곤란하군! 금화 %ld닢! 이 이상은 무리라네!" | 0x532858 "구두쇠에게는 팔 마음 없네! 두번 다시 오지 말게!" |

성공 대사만 `%ld`(깎인 가격)를 받는다.

## 호출자 — 교역 대화 0x481580의 모달 루프

`0x004811E0`의 **xref는 0x4817AB 하나뿐**이다. 매각 쪽 경로에는 없다 —
그래서 **살 때만** 이 메뉴가 뜬다.

```
0x481776  mode = 1
0x48177E  L_pump:  ret = MsgPump()            ; 0x459CC0
0x481787  if (ret != 0x996) goto L_other      ; 0x996 = [결정] 버튼
0x481796  price = GetPrice()                  ; 0x415960
0x48179F  L_menu:  ret = BargainMenu(ctx, &price, &mode)   ; 0x4811E0
0x4817B0  if (!ret) goto L_after
          switch (mode) {
0x4817E6    case 0: DoTrade(...);        break;   // 0x481430 — 실제 매매 성립
0x4817F6    case 1: SetPrice(price);     break;   // 0x415980 — 깎인 값으로 창 갱신
0x481815    case 2: Penalty(ebp, ebx,           // 0x481190 — 결렬 벌칙
                    ctx->tries >= 3 ? 100 : 50); break;
          }
0x48181A  if (mode == 1) goto L_menu           ; 흥정 계속
0x481825  L_after: if (mode == 1) goto L_pump  ; "돌아간다" — 구입창으로
0x481830  goto L_end
```

- 0x481832(L_other)는 [결정]이 아닌 종료 경로다. 흥정만 하고 안 사면
  0x5328C0 "날 바보 취급하는 건가? 살 건지 안 살 건지 빨리하게!"가 나오고,
  0x481874에서 **공급량 10%**를 거둬 간다.

## 벌칙 0x00481190 — 상인이 물건을 거둬 간다

```c
// __thiscall, ret 0xC
void TakeBack(Ctx* ecx, int count, Rec16* list, int pct) {
    for (int i = 0; i < count; i++)
        Sub_481100(ecx, list[i].origin, list[i].kind, list[i].qty * pct / 100);
}
// 0x00481100 __thiscall, ret 0xC — 그 물건을 대는 도시 재고에서 뺀다
//   그 도시 특산품(0x42A030)과 같은 종류면 [도시+0x18] -= qty (0 밑으론 안 내려간다, 0x42A0A0)
//   아니면 공통품 칸에 -qty 를 더한다(0x42A120)
```

`list`는 판매목록 빌더(0x480CC0)가 만든 16바이트 레코드 배열이고, `+4`가 그 줄 공급량이다.

| 언제 | pct | 어디 |
|---|---|---|
| 거래 결렬(실패 두 번째) — 그 전 시도가 3회 미만 | **50** | 0x48180B |
| 〃 — 이미 3회 넘게 걸었으면 | **100** (씨가 마른다) | 0x481804 |
| 흥정만 걸고 안 사고 나감 | **10** | 0x481876 |

인게임으로도 확인했다(2026-08-21 제보): 한 번 깎아 준 뒤 더 깎으려다 실패하면
"이쪽은 바쁘다네. 살 마음이 없으면 돌아가게."가 나오고 **교역소 공급량이 줄어든다** —
성공 1회(tries 0→1) 뒤 실패(tries 1→2)라 `2 >= 2`로 결렬, `2 < 3`이라 50%다.

## 플러그인에서 쓰는 법

MarketUtilKR은 자체 매매창이라 게임 대화 컨텍스트(`Ctx`)가 없다. 그래서 0x4811E0을
통째로 부르지 않고 **컨텍스트가 필요 없는 조각만 그대로 부른다**:

| 부르는 것 | 규약 | 쓰임 |
|---|---|---|
| 0x4A17F0 | `__thiscall int(Ctx*)` | 그 도시가 보는 재주 번호 |
| 0x481400 | `__cdecl int(int, int)` | 흥정 가능 판정(그 재주가 2 이상인가) |
| 0x481330 | `__cdecl int(void)` | 성공 판정(확률표 + 게임 rand) |

0x4A17F0 만 `this` 가 필요한데 `[this+0x90]`(도시 id) 한 자리만 읽으므로, 가격 함수
(`Mkt_GamePrice` → 0x480890)에서 이미 쓰던 수법대로 **그 자리만 채운 껍데기**를 넘긴다.

대사 문자열은 `.data`에서 그대로 읽어(CP949 → UTF-16) 쓴다. 규칙(90% · 성공 3회 · 실패 2회 ·
공급량 벌칙 50/100/10%)은 위 의사코드대로 옮겼다 — `marketdb.c`의 `Mkt_Bargain*` /
`Mkt_SupplyCut`. 재고 자리는 살 때와 같다(공통품 `도시+0x44 + 자리*4`, 특산·수입품 `원산지+0x18`).

메뉴 판은 게임 것과 같은 모양으로 직접 그린다(`market.c`의 `PaintBargain`) — 짙은 자주갈색
바탕(49,24,24)에 크림 테두리(226,214,189) 두 줄, 그 안에 MISC.CDS 띠 단추 셋이 세로로 선다.
오리지널 화면을 재서 비율을 맞췄다(판 156x102, 단추 136x24, 사이 5).
