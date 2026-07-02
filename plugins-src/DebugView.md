# DebugView 사용법

`plugins-src`의 플러그인들은 `OutputDebugStringW`로 로그를 남깁니다. 이 로그를 보려면
Sysinternals의 **DebugView**가 필요합니다.

## 다운로드

- 공식 페이지: https://learn.microsoft.com/sysinternals/downloads/debugview
- Winget이 있다면:
  ```
  winget install Microsoft.Sysinternals.DebugView
  ```

별도 설치 과정 없이 `Dbgview.exe` 단일 실행 파일로 동작합니다.

## 사용 순서

1. **DebugView를 게임보다 먼저 실행**합니다. 플러그인은 게임이 뜨자마자
   (`DLL_PROCESS_ATTACH`) 로그를 찍으므로, 게임을 먼저 켜면 초반 로그를 놓칩니다.
2. 관리자 권한으로 실행하는 것을 권장합니다 (우클릭 → 관리자 권한으로 실행).
   다른 계정/세션의 프로세스를 캡처해야 할 때 필요할 수 있습니다.
3. 메뉴 `Capture → Capture Win32` 체크가 켜져 있는지 확인합니다 (기본값 켜짐).
   커널 이벤트까지 볼 필요는 없으므로 `Capture Global Win32`는 꺼둬도 됩니다.
4. 32비트/64비트는 신경 쓸 필요 없습니다 — DebugView는 커널 드라이버로 캡처하므로
   `CDS95.exe`가 32비트 프로세스여도 그대로 잡힙니다.
5. 로그가 많으면 `Edit → Filter/Highlight...`의 Include 필드에 `CollectionUtilKR*`처럼
   입력해서 원하는 플러그인 로그만 걸러 볼 수 있습니다.
6. `cds95runfile/CDS95.EXE`를 실행합니다. DebugView 창에 실시간으로 로그가 찍힙니다.
7. 필요하면 `File → Save As`로 로그를 텍스트 파일로 저장할 수 있습니다.

## 로그 읽는 법 (CollectionUtilKR 기준)

| 로그 | 의미 |
| --- | --- |
| `[CollectionUtilKR] hook installed.` | 후킹 성공 |
| `[CollectionUtilKR] hook target not found for this version, skipping.` | 해당 버전용 주소가 아직 `NULL`이라 스킵됨 (기본 상태) |
| `[CollectionUtilKR] MH_CreateHook failed.` | 주소를 채워 넣었는데 MinHook이 실패 (주소가 틀렸거나 이미 다른 후킹과 충돌) |
| `[CollectionUtilKR] MH_EnableHook failed.` | 후킹 생성은 됐지만 활성화 실패 |

## 팁

- 로그가 하나도 안 뜬다면 DLL 자체가 로드되지 않은 것일 수 있습니다 — `.plugin`이
  `CDS95Util` 폴더에 정확히 들어갔는지, 확장자가 `.plugin`인지부터 확인하세요.
- 후킹 로직 추가 후 크래시가 나면, 마지막으로 찍힌 로그 라인을 보고 어디까지
  실행됐는지 가늠할 수 있습니다.
