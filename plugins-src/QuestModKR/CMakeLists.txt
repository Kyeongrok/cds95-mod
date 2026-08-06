add_library(QuestModKR SHARED
    src/dllmain.c
    src/questmod.c
)

target_link_libraries(QuestModKR PRIVATE user32 gdi32 comctl32 shell32)

# ListView 매크로를 와이드로 고정(정의 안 하면 ANSI 로 잡혀 한글이 깨진다).
target_compile_definitions(QuestModKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(QuestModKR PRIVATE /utf-8)
endif()

set_target_properties(QuestModKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
