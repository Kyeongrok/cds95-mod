# 플러그인 개발 스캐폴드 (Tier A: 독립형)

`CDS95Util/*.plugin` 바이너리들의 **소스 코드는 이 저장소에 없습니다.**
여기 있는 코드는 그것들을 대체/재구현하기 위한 새 프로젝트의 뼈대입니다.

## 확인된 플러그인 로딩 구조 (PE 정적 분석으로 확인, 100% 확실)

- `cds95runfile/ddraw.dll`은 `CDS95Util/DDrawWrapper.plugin`과 MD5가 동일합니다.
  즉 게임이 로드하는 `ddraw.dll`은 사실 DirectDraw 프록시 DLL이며, 자신의 `DllMain`에서
  `CDS95Util` 폴더를 뒤져 `*.plugin` 확장자를 가진 모든 DLL을 `LoadLibrary`합니다.
- 로더가 요구하는 필수 진입점은 없습니다. `DllMain(DLL_PROCESS_ATTACH)`에서 스스로
  후킹을 끝내면 그걸로 충분합니다 (`HotelUtil.plugin`, `CPUPatch.plugin`,
  `MemoryFix.plugin`, `CDROMUtil.plugin`은 export가 0개입니다).
- `LoadMenuCallback` / `LoadExtraDataCallback` / `SaveExtraDataCallback` 같은 export는
  **선택 사항**이며, 있으면 게임 메뉴·세이브 파이프라인과 연동됩니다
  (`TradeUtil`, `CollectionUtil`, `SaveUtil`에 존재). 이번 Tier A 스캐폴드는
  이 연동을 하지 않고 독립적으로 동작합니다.

## 빌드 준비

1. MinHook 소스를 서브모듈로 추가합니다.
   ```
   git submodule add https://github.com/TsudaKageyu/minhook.git plugins-src/third_party/minhook
   git submodule update --init
   ```
2. CMake + Visual Studio(x86 툴체인)로 빌드합니다. **반드시 32비트로 빌드**해야 합니다
   (CDS95.exe가 32비트 프로세스이므로 인젝션되는 DLL도 32비트여야 함).
   ```
   cmake -S plugins-src -B plugins-src/build -A Win32
   cmake --build plugins-src/build --config Release
   ```
3. 결과물 `plugins-src/build/CollectionUtilKR/Release/CollectionUtilKR.plugin`을
   로컬 테스트용 `cds95runfile/CDS95Util/` 폴더에 복사합니다.
   (`cds95runfile/`은 `.gitignore`에 포함되어 있어 실제 게임 실행 파일/테스트 결과물이
   커밋되지 않습니다.)

## 지금 상태로 빌드하면 어떻게 동작하나요?

`hooks.c`의 후킹 대상 주소가 전부 `NULL`(TODO)이므로, 이 상태로 빌드해서 넣어도
**아무 것도 후킹하지 않고 조용히 넘어갑니다.** [DebugView](DebugView.md)로 보면
`[CollectionUtilKR] hook target not found for this version, skipping.` 로그가 찍힙니다.

즉 1단계 목표는 "DLL이 정상적으로 로드되고 게임이 죽지 않는지" 확인하는 것이고,
그다음부터 실제 후킹 주소/로직을 채워 넣는 순서로 진행하시면 됩니다.

## 한국어판(1.2.0.0) 후킹 주소를 직접 찾는 방법

이 저장소의 `cds95runfile/CDS95.EXE`가 정확히 한국어판 Ver.1.2.0.0입니다
(FileVersionRaw 1.2.0.0, 언어 한국어(대한민국) 확인됨). 추가로 파일을 구할 필요 없이
바로 리버싱 대상으로 쓸 수 있습니다.

권장 도구: **Cheat Engine** (런타임 메모리 추적), **Ghidra 또는 IDA Free** (정적 디스어셈블/디컴파일)

절차 (`CollectionUtil`의 "무제한 창고" 기능 기준 예시):

1. 게임을 실행하고 아이템을 하나 획득합니다.
2. Cheat Engine으로 프로세스에 붙어서, 아이템 개수/ID 값으로 메모리 스캔 →
   아이템을 하나 더 얻어 값이 그만큼 바뀐 주소로 좁혀나갑니다.
   (`5.PatchUtil.md`에 나온 것처럼 이 게임의 아이템/선박 데이터는
   `BaseAddress + RecordSize * Index + Offset` 형태의 고정 배열입니다.)
3. 찾은 주소에 Cheat Engine의 **"Find out what writes to this address"**를 걸고
   아이템을 다시 획득해, 그 값을 써넣는 명령어(및 호출 스택)를 찾습니다.
4. 그 호출자를 따라 올라가 "아이템 추가" 함수의 진짜 시작 주소를 특정합니다.
5. Ghidra로 `cds95runfile/CDS95.EXE`를 열어 해당 함수를 디컴파일하고,
   **인자 개수/타입, 호출 규약(cdecl/stdcall/thiscall, 스택 정리 방식으로 판별)**을 확인합니다.
6. `hooks.c`의 `AddItemToInventory_t` typedef를 실제 시그니처에 맞게 고치고,
   함수 시작 주소를 이미지 베이스 기준 RVA로 계산해 `kAddItemTargets[]`의
   `{ 1200, (LPVOID)0x실제RVA }` 자리에 채웁니다.
7. `DetourAddItemToInventory`에 실제로 원하는 로직(예: 16개 제한 우회)을 구현합니다.

이 과정 없이는 정확한 주소를 알 수 없으므로, 다른 사람이 만든 값을 그대로 베껴서
하드코딩하면 반드시 확인 후 사용하세요 — 원본 데이터가 다르면 크래시/프리징이 발생합니다
(`PatchUtil`이 패치 적용 전 원본 바이트를 검증하는 것도 이 때문입니다).

## 세이브 데이터 연동은 어떻게 하나요?

원본 `CollectionUtil`은 `PatchUtil.plugin`의 `GetExtradata`/`SetExtradata` API로
`EXTRADATA.CDS`/`EXTRADATA.TMP`에 창고 데이터를 저장합니다. 이 ABI는 문서화되어 있지
않아 정확한 호출 규약을 알려면 `PatchUtil.plugin`도 별도로 리버싱해야 합니다(Tier B).

Tier A(지금 이 스캐폴드)에서는 그 대신 표준 C 파일 I/O로 자체 데이터 파일
(예: `CDS95Util\CollectionUtilKR.dat`)을 만들어 쓰는 것을 권장합니다 — 훨씬 단순하고
PatchUtil의 내부 구현에 의존하지 않습니다.

## 배포 시 주의

`0.ReadMe.md`의 재배포 조건(5ch 대항해시대 스레드 한정 재배포/전재 허용, 출처·수정 부분
명시 조건, 상업적 이용 금지)은 원본 CDS95Util 결과물에 적용되는 조건입니다.
직접 새로 작성한 이 플러그인을 배포할 계획이라면 어떤 라이선스로 배포할지 별도로
정하시는 것을 권장합니다.
