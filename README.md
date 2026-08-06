# CDS95Util

[![Release](https://img.shields.io/github/v/release/Kyeongrok/cds95-mod?logo=github)](https://github.com/Kyeongrok/cds95-mod/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Kyeongrok/cds95-mod/total?color=brightgreen)](https://github.com/Kyeongrok/cds95-mod/releases)

『대항해시대 III』 Windows 95판(CDS95) 유틸리티 모음입니다.
플러그인 방식으로 구성되어 있어 필요한 기능만 선택해서 사용할 수 있습니다.
각 플러그인의 상세 설명은 [0.ReadMe.md](0.ReadMe.md) 및 번호가 매겨진 문서들을 참고하세요.

## 설치 방법

1. [Releases](../../releases) 페이지에서 최신 zip 파일을 받습니다.
2. 압축을 풀면 나오는 `ddraw.dll`과 `CDS95Util` 폴더를 CDS95.exe(게임 실행 파일)와 같은 폴더에 복사합니다.
3. CDS95.exe와 같은 폴더에 `ddraw.dll`이 있으면 정상적으로 설치된 것입니다.
   - 이미 `ddraw.dll`이 존재하는 경우(다른 유틸리티 등)에는 이 유틸리티를 사용할 수 없습니다.
4. 일부 플러그인은 설정 파일이나 저장 데이터를 기록하므로 설치 폴더에 쓰기 권한이 필요합니다.
   `Program Files` 등 쓰기 권한이 제한된 경로에 설치했다면 다른 경로로 재설치하거나 폴더 권한을 수정하세요.

## 한국어판 호환성

이 유틸리티는 원래 일본판 CDS95.exe(Ver.1.1.0.0 / 1.2.7.0 / 1.4.0.0)를 대상으로 개발되었습니다.
한국어판(Ver.1.2.0.0)에 대한 호환성은 플러그인별로 다릅니다.

| 플러그인 | 방식 | 한국어판 호환 가능성 |
| --- | --- | --- |
| DDrawWrapper | DirectDraw API 대체(래핑), 게임 내부 주소 미참조 | 높음 |
| MemoryFix | 범용 메모리 버그 우회 | 높음 |
| PatchUtil | 외부 텍스트 파일 기반 동적 패치(주소는 파일에서 읽음) | 높음 (단, 패치 파일 자체는 버전별로 새로 작성 필요) |
| MouseUtil | PatchUtil을 통한 메시지 후킹 | 미확인 |
| CPUPatch | 바쁜 대기 루프 패치, 일부 버전 하드코딩 | 낮음~중간 |
| HotelUtil, TradeUtil, CollectionUtil, SaveUtil | 게임 내부 메모리 주소(일본판 기준) 하드코딩 | 낮음 (크래시 가능성) |

한국어판에서 문제가 발생하는 플러그인은 `CDS95Util` 폴더에서 해당 `.plugin` 파일을 제거하면 나머지 기능은 그대로 사용할 수 있습니다.

### HotelUtilKR (한국어판 전용 재구현)

`HotelUtilKR.plugin`은 한국어판 `cds_95.exe`를 직접 리버싱해 새로 작성한 플러그인입니다.
여관 "숙박"이 기본 30일 지나가는 것을 MinHook으로 가로채, **숙박 일수를 직접 입력**받아
그만큼만 숙박하도록 합니다(숙박비도 일수에 비례). 여관 "숙박"을 누르면 일수 입력창이 뜨고,
확인하면 그 일수만큼, 취소하면 원래대로 30일 진행됩니다. 원본 일본판 `HotelUtil.plugin`과는
별개 파일입니다.

## 개발 가이드 (플러그인 직접 개발·수정)

이 저장소에는 배포된 `.plugin` 바이너리의 원본 소스가 없습니다. 한국어판 호환성 문제를
직접 고치거나 새 플러그인을 만들고 싶다면 `plugins-src/` 폴더의 스캐폴드로 시작하세요.
MinHook 기반으로 새 DLL을 처음부터 작성하는 방식이며, 기존 `.plugin`을 역어셈블하는 것이
아닙니다.

### 빌드 방법

1. MinHook 소스를 서브모듈로 추가합니다.
   ```
   git submodule add https://github.com/TsudaKageyu/minhook.git plugins-src/third_party/minhook
   git submodule update --init
   ```
2. CMake로 **32비트(x86)** 빌드합니다. CDS95.exe가 32비트 프로세스이므로 반드시
   `-A Win32`를 지정해야 합니다.
   ```
   cmake -S plugins-src -B plugins-src/build -A Win32
   cmake --build plugins-src/build --config Release
   ```
3. 결과물: `plugins-src/build/CollectionUtilKR/Release/CollectionUtilKR.plugin`

### 테스트 방법

1. 실제 게임 실행 파일은 저장소에 포함되어 있지 않고 `cds95runfile/`(`.gitignore` 처리됨)에
   로컬로만 존재합니다. `CDS95.exe`, `ddraw.dll`(= `DDrawWrapper.plugin` 복사본),
   `CDS95Util/` 폴더가 그 안에 준비되어 있어야 합니다.
2. 빌드한 `.plugin` 파일을 `cds95runfile/CDS95Util/`에 복사합니다.
3. `cds95runfile/CDS95.EXE`를 실행하고, **DebugView**로 `OutputDebugString` 로그를
   확인해 DLL이 정상 로드/후킹되었는지 확인합니다. 다운로드 링크와 사용법은
   [plugins-src/DebugView.md](plugins-src/DebugView.md) 참고.
4. 크래시가 나면 방금 넣은 `.plugin`만 폴더에서 빼고 재현되는지 확인해 원인을 격리합니다.
   (다른 플러그인과 마찬가지로, 문제가 있는 `.plugin` 파일 하나만 제거해도 나머지는 정상
   동작합니다.)

실제 후킹 주소를 한국어판 기준으로 찾는 리버싱 절차(Cheat Engine + Ghidra)는
[plugins-src/README.md](plugins-src/README.md)에 단계별로 정리되어 있습니다.

### 릴리즈 / 버전 관리

버전의 단일 출처는 저장소 루트의 [`VERSION`](VERSION) 파일입니다. 릴리즈 절차:

1. `VERSION` 파일을 새 버전으로 수정합니다 (예: `0.1.0`).
2. 배포할 `.plugin` 바이너리를 빌드해 `CDS95Util/`에 갱신합니다
   (`.github/workflows/release.yml`이 `CDS95Util/` 폴더를 그대로 패키징합니다).
3. `VERSION`과 같은 값으로 태그를 만들어 push하면 릴리즈 워크플로가 zip을 만들어
   [Releases](../../releases)에 올립니다.
   ```
   git tag "v$(cat VERSION)"
   git push origin "v$(cat VERSION)"
   ```

## 재배포·개조·전재 조건 및 사용 라이브러리

라이선스 및 외부 라이브러리(MinHook, Zip Utils) 관련 내용은 [0.ReadMe.md](0.ReadMe.md)를 참고하세요.

### 폴더 배치

압축을 풀면 이렇게 깔립니다.

```
CDS95Util/
    ModUtilKR.plugin            루트 고정 - 아래 plugins 폴더를 대신 불러온다
    cities.json  discoveries.json   데이터는 루트 한 자리에 모은다
    plugins/
        록히드매튜/              KR 플러그인 8개
    mods/
        <모드>/
            mod.txt             all= / base= / 설명
            quests/             *.CDS + quests.json  (퀘스트 모드)
            patches/            *.json               (메모리 패치)
```

모드는 퀘스트만, 패치만, 둘 다 가질 수 있습니다. 퀘스트가 없는 모드는 [퀘스트 모드] 창에
뜨지 않고, 패치는 [패치] 창에 만든이와 함께 나옵니다.

게임 메뉴는 `파일` 아래 여섯입니다.

```
정보      항해사 찾기 / 퀘스트 / 소지품 / 여급 / 스폰서 / 도감 / 교역 / 교역품
지도      함선      워프 >
모드 >    플러그인 관리 · 퀘스트 모드 · 패치
업데이트
```

로더(`ddraw.dll`)는 `CDS95Util` 루트의 `*.plugin` 만 불러옵니다. 그 파일은 고칠 수 없으므로,
만든이별 폴더에 둔 플러그인은 `ModUtilKR`이 `LoadLibrary`로 대신 불러옵니다. 그래서
`ModUtilKR.plugin`과 `ddraw.dll`은 루트에 있어야 합니다. 나머지는 폴더를 나눠도 되고
루트에 그대로 둬도 됩니다.

플러그인이 `plugins/<만든이>/`에 있어도 `cities.json` · `quests.json` · `mods/` 같은 데이터는
`CDS95Util` 루트에서 찾습니다. 흩어진 플러그인이 같은 자료를 보게 하려는 것입니다.

### 패치 json 적는 법

`mods/<만든이>/patches/*.json` 에 넣으면 [패치] 창에 뜹니다. 한 파일에 하나(`{...}`)든
여럿(`[...]`)이든 됩니다.

```json
{
  "Name": "용어: 웅변→변론",
  "Addresses": ["0x15E0DC"],
  "Type": "toggle",
  "ValueType": "text",
  "OriginalValue": "웅변",
  "PatchedValue": "변론",
  "AutoApply": true
}
```

`ValueType` 은 값을 어떻게 적었는지 밝히는 것입니다.

| 값 | 적는 법 | 보기 |
|---|---|---|
| `text` (`글자`) | 한글 그대로. cp949 로 바뀌어 들어갑니다 | `"웅변"` |
| `hex` (`16진수`) | 바이트열 | `"BF F5 BA AF"` |
| `number` (기본) | 숫자 | `2948265407` |

안 적으면 알아서 가립니다(따옴표 안이 16진수 글자뿐이면 바이트열, 아니면 글자).
`ByteSize` 도 글자 길이에서 나오므로 생략할 수 있고, 원본과 패치본의 길이가 다르면 짧은 쪽을
공백(`0x20`)으로 채워 맞춥니다.

`OriginalValue` 를 **적지 않으면** 지금 값이 무엇이든 그냥 씁니다. 문구가 자리마다 다를 때
쓰세요(실제로 `선두상` 14곳 중 2곳은 원본 EXE 가 이미 `선수상` 이었습니다). 체크를 풀면
게임을 켰을 때 그 자리에 있던 바이트로 **자리마다 따로** 되돌립니다.

`AutoApply: true` 는 창을 열기 전, 플러그인이 뜨는 순간 적용합니다. 게임이 글자를 자기
버퍼로 옮겨 간 뒤에 고치면 화면에 안 나타나므로 문구 패치에는 대개 이것이 필요합니다.

### 함께 담은 퀘스트 모드

`CDS95Util/mods/kseokjeong_quest_mod/`는 kseokjung님이 만드신
[대3 퀘스트패치 v1.5](https://cafe.naver.com/daehangs)를 그대로 담은 것입니다. 만든 이와
출처는 해당 폴더의 `mod.txt`에 적어 두었습니다.

게임 원본 이벤트 파일(바닐라)은 담지 않습니다. `QuestModKR`이 처음 실행될 때 각자의 게임
폴더에서 떠서 `mods/default_quest_mod/`를 만듭니다.
