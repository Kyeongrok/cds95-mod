add_library(HintUtilKR SHARED
    src/dllmain.c
    src/hint.c
    src/hintdb.c
    src/disc.c
    src/patronpick.c
    # 창은 다른 KR 창들과 같은 세피아 판이다 — 그리기 도구를 복사하지 않고 원본을 같이 빌드한다.
    ../CharacterUtilKR/src/uikit.c
    # 오른쪽 판의 스폰서 — 후원자 표와 초상화도 CharacterUtilKR 것을 그대로 같이 빌드한다.
    ../CharacterUtilKR/src/chardb.c
    ../CharacterUtilKR/src/patrons.c
    ../CharacterUtilKR/src/faces.c
    ../CharacterUtilKR/src/ls12.c
    ../CharacterUtilKR/src/imgio.c
)

target_include_directories(HintUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../TradeUtilKR/src   # warp_data.h — 도시별 워프 값
)

# 후킹이 없다 — 모듈 안의 표를 읽어 그리기만 한다.
target_link_libraries(HintUtilKR PRIVATE user32 gdi32 comdlg32)

target_compile_definitions(HintUtilKR PRIVATE UNICODE _UNICODE)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(HintUtilKR PRIVATE /utf-8)
endif()

set_target_properties(HintUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll)가 CDS95Util 폴더에서 *.plugin 만 스캔한다.
)
