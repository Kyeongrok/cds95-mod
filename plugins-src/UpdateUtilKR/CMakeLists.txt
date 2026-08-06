add_library(UpdateUtilKR SHARED
    src/dllmain.c
    src/update.c
)

# wininet — GitHub 릴리즈 목록과 zip 을 받는 데 쓴다(윈도에 늘 있고 HTTPS·리다이렉트를 알아서 한다).
target_link_libraries(UpdateUtilKR PRIVATE user32 gdi32 comctl32 shell32 wininet)

# ListView 매크로를 와이드로 고정(정의 안 하면 ANSI 로 잡혀 한글이 깨진다).
target_compile_definitions(UpdateUtilKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(UpdateUtilKR PRIVATE /utf-8)
endif()

set_target_properties(UpdateUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
