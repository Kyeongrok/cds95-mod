add_library(CharacterUtilKR SHARED
    src/dllmain.c
    src/character.c
    src/navview.c
    src/questview.c
    src/invview.c
    src/inventory.c
    src/playerview.c
    src/imgio.c
    src/questjson.c
    src/questdb.c
    src/savedata.c
    src/chardb.c
    src/maids.c
    src/patrons.c
    src/charstate.c
    src/livechar.c
    src/faces.c
    src/uikit.c
    src/ls12.c
)

# comdlg32: PNG 파일 고르는 대화상자. gdiplus 는 링크하지 않고 실행 중에 부른다
# (없는 환경에서 플러그인 자체가 안 뜨는 일을 막으려는 것 — imgio.c 참고).
target_link_libraries(CharacterUtilKR PRIVATE user32 gdi32 comdlg32)

# 도시 이름표(kCities)는 TradeUtilKR 쪽 cities_data.h 를 그대로 쓴다.
# 같은 표를 두 벌 두면 한쪽만 고쳐지는 사고가 나므로 복사하지 않고 참조한다.
target_include_directories(CharacterUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TradeUtilKR/src
)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(CharacterUtilKR PRIVATE /utf-8)
endif()

set_target_properties(CharacterUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
