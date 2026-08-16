add_library(SkillUtilKR SHARED
    src/dllmain.c
    src/skilldb.c
    src/skillwin.c
)

# 도시 이름표(kCities)는 TradeUtilKR 쪽 cities_data.h 를 그대로 쓴다.
# 같은 표를 두 벌 두면 한쪽만 고쳐지는 사고가 나므로 복사하지 않고 참조한다.
# (그 파일의 lib/ship/guild 열은 EXE 건물표와 어긋나므로 이름만 쓴다.)
target_include_directories(SkillUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../TradeUtilKR/src
)

target_link_libraries(SkillUtilKR PRIVATE user32 gdi32 comctl32 shell32)

target_compile_definitions(SkillUtilKR PRIVATE UNICODE _UNICODE)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(SkillUtilKR PRIVATE /utf-8)
endif()

set_target_properties(SkillUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
