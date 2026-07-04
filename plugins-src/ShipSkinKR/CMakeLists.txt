add_library(ShipSkinKR SHARED
    src/dllmain.c
    src/hook.c
)

target_link_libraries(ShipSkinKR PRIVATE minhook)

# 한글 주석(UTF-8) — MSVC cp949 오독으로 주석이 다음 줄 삼키는 것 방지
target_compile_options(ShipSkinKR PRIVATE /utf-8)

set_target_properties(ShipSkinKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll=DDrawWrapper.plugin)가 CDS95Util에서 *.plugin 만 스캔
)
