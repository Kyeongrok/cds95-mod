add_library(HotelUtilKR SHARED
    src/dllmain.c
    src/version.c
    src/hooks.c
    src/dialog.c
)

target_link_libraries(HotelUtilKR PRIVATE minhook version shlwapi user32 gdi32)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(HotelUtilKR PRIVATE /utf-8)
endif()

set_target_properties(HotelUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 게임 로더(ddraw.dll = DDrawWrapper.plugin)가 CDS95Util 폴더에서 *.plugin 확장자만 스캔합니다.
)
