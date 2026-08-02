add_library(WorldMapKR SHARED
    src/dllmain.c
    src/menu.c
    src/mapwin.c
    src/world.c
    src/citydb.c
    src/discdb.c
    ../CharacterUtilKR/src/uikit.c
)

target_link_libraries(WorldMapKR PRIVATE user32 gdi32)

# 세피아 색표와 위젯(uikit)은 CharacterUtilKR, 도시 이름표(kCities)는 TradeUtilKR 쪽 것을
# 그대로 쓴다. 복사해 두면 한쪽만 고쳐지는 사고가 나므로 소스를 참조한다.
target_include_directories(WorldMapKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../TradeUtilKR/src
)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(WorldMapKR PRIVATE /utf-8)
endif()

set_target_properties(WorldMapKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
