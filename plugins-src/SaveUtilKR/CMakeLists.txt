add_library(SaveUtilKR SHARED
    src/dllmain.c
    src/save.c
)

# 후킹이 없다 — 메뉴를 붙이고 게임 함수를 그대로 부르는 것이 전부다.
target_link_libraries(SaveUtilKR PRIVATE user32)

target_compile_definitions(SaveUtilKR PRIVATE UNICODE _UNICODE)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(SaveUtilKR PRIVATE /utf-8)
endif()

set_target_properties(SaveUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll)가 CDS95Util 폴더에서 *.plugin 만 스캔한다.
)
