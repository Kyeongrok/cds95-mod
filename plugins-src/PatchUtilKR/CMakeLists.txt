add_library(PatchUtilKR SHARED
    src/dllmain.c
    src/patch.c
)

target_link_libraries(PatchUtilKR PRIVATE user32 gdi32 comctl32)

# ListView 등 공용컨트롤 매크로(WC_LISTVIEW, LVM_*, LVITEM/LVCOLUMN)를 와이드로 고정.
# (정의 안 하면 ANSI 로 잡혀 와이드 구조체와 불일치 → 한글 깨짐/컬럼 오동작)
target_compile_definitions(PatchUtilKR PRIVATE UNICODE _UNICODE)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(PatchUtilKR PRIVATE /utf-8)
endif()

set_target_properties(PatchUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
