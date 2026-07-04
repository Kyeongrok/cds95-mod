add_library(ScreenUtilKR SHARED
    src/dllmain.c
    src/screen.c
)

target_link_libraries(ScreenUtilKR PRIVATE minhook gdi32 user32)

# 소스가 UTF-8(한글 주석) — MSVC가 cp949로 오독해 주석이 다음 줄을 삼키는 것 방지.
target_compile_options(ScreenUtilKR PRIVATE /utf-8)

set_target_properties(ScreenUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll=DDrawWrapper.plugin)가 CDS95Util에서 *.plugin 만 스캔
)
