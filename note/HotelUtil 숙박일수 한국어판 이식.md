# 대항해시대3 숙박일수 선택 기능 — 한국어판 이식 작업 로그

관련: [[프로젝트 개요]] · [[feedback]]

## (2026-07-02): 년/월/일 라이브 주소 확정 ★

차등(differential) 메모리 스캔으로 **한국어판(cds_95.exe) 라이브 년/월/일 주소를 확정**했다.
게임 안에서 날짜를 1480/6/19 → 1480/7/9 로 진행시킨 뒤, 값이 19→9 로 바뀐 주소를
전체 메모리 93,077개 후보 중 딱 1개로 좁혀서 특정했고, 그 주변을 덤프해서 년·월도 확인:

```
Year  (i32) = 0x005A4D20   ← c8 05 00 00 = 1480
Month (i32) = 0x005A4D24   ← 07 00 00 00 = 7
Day   (i32) = 0x005A4D28   ← 09 00 00 00 = 9   (차등 스캔으로 유일하게 특정된 주소)
```


도구 (리포 `plugins-src/tools/`):
- `verify-date-addresses.ps1` — 위 3개 주소를 i32로 읽어 게임 표시 날짜와 대조(PASS/FAIL).
  예: `.\verify-date-addresses.ps1 -Year 1480 -Month 7 -Day 9`
- `find-date-by-diff.ps1` — 빌드가 또 바뀌어 주소가 어긋나면 재탐색용(capture→하루진행→diff).

**다음 할 일**: (2) HotelUtil 후킹 포인트 — 여관 "숙박" 누르는 순간을 어떻게 가로채는지.
이제 현재 년/월/일을 읽을 수 있으니 "NXT=다음 달까지 남은 일수" 계산은 바로 가능.

## ★ 2차 진행 (2026-07-02): fb2 = MinHook으로 +30일 가로채기 ★

fb2 확정 요구: 여관 "숙박" → **기본 30일**이 지나감. MinHook으로 30일 증가 직전에
게임 "계산기(숫자 입력) UI"로 원하는 일수 N을 받아 **N일만 숙박**되게 하기.

이번에 추가로 확정한 것:
- **소지금 주소 = 0x005B6194 (i32)** — 차등(41829→41681)으로 유일 특정.
- **숙박 1회 = 정확히 +30일** — 1480/7/9 에서 숙박 후 1480/8/8 (=30일), 비용 148골드.
- 소지금은 절대주소로 직접 write 됨: `mov [0x5B6194], eax` 형태가 .text에 10곳
  (0x4044B2, 0x4044F0, 0x40477C, 0x4047D1, 0x405DA5, 0x405E8E, 0x405FC1(=0),
   0x40600C, 0x4060AD, 0x40A238) — 단, 여러 거래가 공유하는 "소지금 세팅" 헬퍼로 보임.

**핵심 난점 (정적 분석의 한계):**
- 날짜(년/월/일)는 **절대주소로 write 하는 코드가 0곳**. 읽기/비교(a1/8b/3b/2b/81 3d)는
  절대주소로 많지만, **쓰기는 전부 구조체 포인터+오프셋 경유**. 상수 30(0x1E)도 .text
  전체에 immediate로 22곳뿐이고 날짜/소지금 근처엔 무관한 것(점프테이블)뿐.
  → **정적 XREF만으로는 +30 하는 숙박 루틴을 특정 불가.** 동적 추적이 필요.

**MinHook 대상(=날짜 갱신 루틴)을 찾는 두 갈래:**
1. **동적 write 추적** — 게임에서 숙박하는 순간 0x5A4D28(day)에 write 하는 명령을
   하드웨어 브레이크포인트로 잡기(치트엔진 "이 주소에 쓰는 코드 찾기"와 동일).
   가장 확실하나 라이브 게임에 디버거 attach 필요(진행 저장 후 권장).
2. **Ghidra/IDA 정적 심층분석** — cds_95.exe 로드해서 날짜 구조체 포인터를 따라
   갱신 루틴을 찾기. XREF가 아니라 데이터 흐름 추적.

도구(리포 `plugins-src/tools/`): `snapshot-diff.ps1`(범용 차등), `scan-code-xrefs.ps1`
(절대주소 참조 코드 찾기), `find-stay-routine.py`(capstone: 30로드+날짜/소지금 근접 후보).

### ★ 3차 진행 (2026-07-02): 숙박 호출 체인 완전 특정 (fb3) ★

내 커스텀 HW-BP 추적기(find-what-writes.py)는 WOW64에서 게임을 **크래시**시킴(예외 처리
까다로움). → **Cheat Engine의 "find what writes to this address"**로 안전하게 잡음(fb3 이미지).
CE 결과: `0x0044B12C - mov [esi+0x10],ebx` 가 히트 **30회** (= day 를 하루씩 30번 씀),
**esi = 0x5A4D18 = 날짜 구조체 base** (day=[esi+0x10]). 여기서부터 capstone 정적분석으로 역추적:

**전체 호출 체인:**
```
여관 메뉴 핸들러 (함수 @ ~0x0047FC00)
  0x0047FC96: push 1          ; mode
  0x0047FC9A: push 0x1E       ; ★ 숙박 "30일" 리터럴 (바이트 6A 1E) ★
  0x0047FC9C: call 0x004A2AD0 ; Rest(this, days=30, mode=1)  (리턴주소 0x0047FCA1)
      ├ 숙박비 차감: 0x00474030 / 0x00474060 (days, days*3 기반)
      └ 0x004A2B1F: call 0x0044AFD0  ; AdvanceDays(this=0x5A4D18, days=30)
             └ days 회 루프(0x44B120~0x44B244): day++ + 월/년 롤오버 + 매일 게임 업데이트
                0x0044B12C: mov [esi+0x10],ebx   ; CE가 잡은 write
```

**핵심 함수/주소:**
- `AdvanceDays(this, days)` = **0x0044AFD0** — __thiscall(ecx=날짜구조체), 스택 arg0=days.
  0x44B0F5에서 `mov edi,[esp+0x18]`=days, edi 회 루프. `ret 4`.
- `Rest(this, days, mode)` = **0x004A2AD0** — 숙박비 차감 후 AdvanceDays 호출. `ret 8`.
  콜러 4종: **0x47FC9A=Rest(30,1)=여관**(유일 30 고정), 0x47FDB2=Rest(365,1),
  0x4770F2/0x46875A=Rest(10,·), 나머지=계산값.
- 날짜 구조체 base **0x5A4D18**: +0x08 year, +0x0C month, +0x10 day.

**fb2 MinHook 계획 (권장):**
- **대상 = 0x004A2AD0 (Rest), 리턴주소로 여관만 필터.**
  detour에서 `[esp]`(리턴주소)==**0x0047FCA1**(여관)이면 → 계산기 UI로 N 입력받아
  days 인자(`[esp+4]`)를 N으로 교체 후 원본 호출. 그러면 여관만 N일 숙박,
  숙박비도 Rest가 days 기반이라 자동 비례. (다른 Rest(항해/이벤트)는 영향 없음)
- 대안(더 서지컬, 비-MinHook): 0x0047FC9B 바이트(0x1E)를 코드케이브로 리다이렉트해
  계산기 값 push. MinHook은 함수 후킹용(5바이트)이라 2바이트 push엔 부적합 → Rest 후킹이 정석.
- **남은 RE 1건**: fb2의 "계산기(숫자입력) UI"를 detour에서 어떻게 띄워 N을 받을지 —
  게임 내 숫자입력 함수 특정 필요(별도 작업).

도구 추가: `find-what-writes.py`(HW-BP 추적기 — WOW64 크래시 주의, CE 권장),
`scan-code-xrefs.ps1`, `find-stay-routine.py`.

### ★ 4차 진행 (2026-07-02): MinHook 플러그인 스켈레톤 (fb4) ★

**계산기 UI 주소도 확보**(fb3 두번째 이미지, CE "find what accesses 0x0019D180"):
계산기 현재 입력값 = `[계산기객체 + 0xA8]`, 관련 메서드 전부 0x482xxx 대
(0x482555 write, 0x482899 read 등). → 나중에 이 계산기를 detour에서 호출해 N 받을 예정.

fb4 방향대로 **계산기 연결 전에 후킹 파이프라인부터 검증하는 스켈레톤** 작성/빌드/배포 완료:
- 새 플러그인 **`plugins-src/HotelUtilKR/`** — Rest(0x4A2AD0) MinHook.
  detour에서 `_ReturnAddress()==0x47FCA1`(여관)일 때만 days 를 **고정 7일**로 교체.
  Rest 는 __thiscall → `__fastcall(ecx,edx,days,mode)`로 모델링(edx 더미), ret 8 일치.
- **시그니처 검증**: 0x4A2AD0=`8B 44 24 08 56 83 F8 02`, 0x47FC9A=`6A 1E` 확인 후에만 훅 설치.
  안 맞으면 조용히 skip → 다른 빌드에서 크래시 방지. (exe 실제 바이트와 전부 일치 확인함)
- MinHook 서브모듈 clone, CMake -A Win32 로 빌드 → PE32 DLL 확인.
  `HotelUtilKR.plugin` 을 `대항해시대3/CDS95Util/` 에 배포.
- build.ps1 타깃 목록에 HotelUtilKR 추가.

**검증 방법**: DebugView 켜고 게임 재시작 → 로드시 `[HotelUtilKR] hook installed`,
여관 숙박시 `hotel stay intercepted` 로그 + 실제로 30일 아닌 **7일**만 지나면 성공.
→ 검증 성공(7일 확인). v0.1.0 릴리즈.

### ★ 5차 진행 (2026-07-02): 숙박 일수 입력 완성 (v0.2.0) — 기능 완료 ★

**게임 계산기 재사용은 포기하고 C++ 모달 다이얼로그로 결정.** 이유:
- 계산기 값 = `[계산기객체+0xA8]`, 클래스 메서드 0x482xxx(HandleKey 0x4824E0, Render 0x482880).
- vtable base 0x5194C0 — 슬롯 수십 개짜리 **대형 UI 화면 클래스**. 게임 프레임 루프에
  물려 매 프레임 Render/입력 처리되는 구조라, **동기 Rest 훅(프레임 사이) 안에서 모달로
  호출 불가**. 재현하려면 객체 생성+렌더/입력 펌프+DDraw 상태까지 복제해야 해 리스크 큼.
- 대신 **자체 메시지 루프를 갖는 Win32 모달 다이얼로그**(DialogBoxIndirectParam, in-memory
  템플릿)를 만들어 훅에서 동기 호출. 게임이 창모드(제목표시줄 확인)라 위에 정상 표시.

구현: `plugins-src/HotelUtilKR/src/dialog.c` — `HotelKR_AskDays(기본30)`. 확인→입력값(1~999),
취소→기본값 유지. Rest 훅에서 `days = HotelKR_AskDays(days)`. CMake에 user32 링크 + `/utf-8`.

**테스트 성공**: 여관 숙박→입력창→입력한 일수만큼만 숙박, 숙박비도 비례(Rest가 days 기반).
→ **v0.2.0 릴리즈 완료** (GitHub Actions, CDS95Util-v0.2.0.zip).
fb1/fb2 기능 목표 달성. (원하면 향후: 게임 계산기 룩앤필 재현은 별도 과제.)

---

리포: `C:\Users\Administrator\git\wpf\cds95-mod` (CDS95Util, 플러그인 원본/재구현)
참고 리포: `C:\Users\Administrator\git\wpf\cds-helper` (세이브 뷰어 + 메모리 리더 + EXE 패치 도구)

## 목표

여관에서 "숙박" 누르면 숙박일수(1~127일)를 매번 직접 입력하는 기능(원본 `HotelUtil.plugin`)을
한국어판(Ver.1.2.0.0)에서도 동작하도록 만들기.

## 원본 HotelUtil 동작 방식 (`1.HotelUtil.md` 기준)

- 숙박일수 입력은 게임 계산기 화면이 아니라 **플러그인 자체 팝업 창**
- NXT 버튼 → "다음 달까지 남은 일수" 자동 입력 (교역품 보충용)
- 게임 내 현재 연/월/일 정보가 필요 (원문: gponys님의 SSG 참고해서 구현)
- PatchUtil을 통한 윈도우 메시지 후킹 사용 (자체 함수 후킹 아님)
- 지원 버전: 1.2.7.0, 1.4.0.0뿐. 한국어판은 원래 미지원.

## 오늘 한 일

### 1. 기존 HotelUtil.plugin 정적 분석 (일본판 1.4.0.0 기준)

`CDS95Util/HotelUtil.plugin`을 바이트 스캔해서 버전 분기 로직과 하드코딩 주소 확인.
버전 비교는 `CMP EAX, imm32` 한 방(예: `0x01040000` = Ver.1.4.0.0)으로 판별.

1.4.0.0 분기에서 찾은 주소 3개 (연속 DWORD, 4바이트 간격):
```
0x0057CA90
0x0057CA94
0x0057CA98
```

일본판 1.4.0.0 실행 파일(로컬 `cds95runfilejp/CDS95.EXE`, 리포에는 커밋 안 함 — 저작권)로
교차 검증:
- PE 섹션 파싱 결과 3개 다 `.data` 섹션 중 파일엔 실체가 없고 런타임에 0으로 채워지는
  영역(사실상 BSS)에 위치 → **함수가 아니라 데이터**
- `.text` 섹션에서 이 3개 주소를 직접 참조하는 코드를 원시 바이트로 스캔한 결과
  50건/26건/12건 — 게임 전역에서 활발히 쓰이는 "진짜 라이브 상태값"
- 문서에서 "연·월·일 정보가 필요했다"고 명시한 것과 종합하면, **이 3개 주소 = 게임 내
  현재 년/월/일 구조체**일 가능성이 높음 (순서는 아직 미확정)

### 2. `cds-helper` 프로젝트에서 기존 자산 발견 (핵심)

이미 한국어판(두기 무설치판) 대상으로 검증된 것들이 있었음:

**`CdsHelper.Support/Local/Helpers/GameMemoryReader.cs`** — 실시간 메모리 읽기, 검증된 주소:
```
CellXAddress        = 0x0019EEE0   (보트 월드 좌표 X)
CellYAddress        = 0x0019EEE4   (보트 월드 좌표 Y)
CurrentCityIdAddress = 0x005B6154  (현재 입항 도시 ID, 1바이트)
```

**`CdsHelper.Support/Local/Helpers/SaveDataService.cs`** — `SAVEDATA.CDS` 세이브 파일
오프셋 (이미 검증됨):
```
YEAR_OFFSET  = 0x15   (uint16, 2바이트)
MONTH_OFFSET = 0x19   (1바이트)
DAY_OFFSET   = 0x1A   (1바이트)
```
같은 서비스에서 `PlayerData.CurrentCity`는 파일 오프셋 `0x57`.

### 3. 라이브 메모리 주소 가설 (❌ 폐기됨 — 위 "검증 완료" 참조. 형식·주소 둘 다 틀렸음)

세이브 파일 구조가 라이브 메모리 구조와 거의 그대로 대응한다는 가정하에:

```
liveStructBase = CurrentCityIdAddress - 0x57 = 0x005B6154 - 0x57 = 0x005B60FD

Year  (live) = liveStructBase + 0x15 = 0x005B6112
Month (live) = liveStructBase + 0x19 = 0x005B6116
Day   (live) = liveStructBase + 0x1A = 0x005B6117
```

**다음에 게임 켜지면 제일 먼저 할 일**: 이 3개 주소를 읽어서 실제 게임 내 표시 날짜와
일치하는지 확인. `CdsHelper.MemoryScanner`(`Program.cs`의 `S <주소hex>` 명령) 또는
`GameMemoryReader` 패턴 그대로 재사용하면 바로 테스트 가능.

## 아직 안 끝난 것

1. ~~년/월/일 라이브 주소 가설 검증~~ ✅ 완료 (2026-07-02, 위 "검증 완료" 참조)
2. HotelUtil이 "숙박일수 입력을 언제 가로채는지"(후킹 포인트) — 데이터 주소 스캔으로는
   안 잡힘. 실제 여관에서 "숙박" 누르는 순간을 CE/MemoryScanner로 관찰해야 함
3. "자국 아닌 숙소에서도 세이브 허용"은 별개 기능 — 아마 조건분기 하나 패치, 별도 확인 필요
4. `cds-helper`의 `CdsHelper.Main` EXE 패치 도구([[무제]] 참고)가 이미 주소 검증/기록 UI를
   갖추고 있어서, 새 패치 항목 등록 방식으로 재사용 가능해 보임

## 참고: HotelUtil.plugin 외 다른 플러그인 주소 분석

전체 내용은 리포의 `plugins-src/existing-plugin-addresses.md` 참고 (CollectionUtil,
TradeUtil 주소도 같은 방식으로 분석해둠).
