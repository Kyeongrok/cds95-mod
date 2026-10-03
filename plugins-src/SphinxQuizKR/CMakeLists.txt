add_library(SphinxQuizKR SHARED
    src/dllmain.c
)

# 창도 메뉴도 없다 — 게임 코드의 call 두 자리만 돌린다.
target_link_libraries(SphinxQuizKR PRIVATE user32)

# 한글 주석(UTF-8) — MSVC cp949 오독으로 주석이 다음 줄 삼키는 것 방지
if(MSVC)
    target_compile_options(SphinxQuizKR PRIVATE /utf-8)
endif()

set_target_properties(SphinxQuizKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll)가 CDS95Util 폴더에서 *.plugin 만 스캔한다.
)
