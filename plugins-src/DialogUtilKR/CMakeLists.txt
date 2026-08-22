add_library(DialogUtilKR SHARED
    src/dllmain.c
    src/dialog.c
)

target_include_directories(DialogUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common          # modmenu.h — 모드 창 등록부 약속
)

target_link_libraries(DialogUtilKR PRIVATE user32 gdi32 comctl32 shell32)

# ListView 매크로(WC_LISTVIEW, LVM_*, LVITEM/LVCOLUMN)를 와이드로 고정.
# (정의 안 하면 ANSI 로 잡혀 와이드 구조체와 불일치 → 한글 깨짐/컬럼 오동작)
target_compile_definitions(DialogUtilKR PRIVATE UNICODE _UNICODE)

# 소스에 한국어(UTF-8) 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(DialogUtilKR PRIVATE /utf-8)
endif()

set_target_properties(DialogUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
