add_library(GameApiKR SHARED
    src/dllmain.c
    src/api.c
)

# 한글 주석(UTF-8) — MSVC cp949 오독 방지
target_compile_options(GameApiKR PRIVATE /utf-8)

set_target_properties(GameApiKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
