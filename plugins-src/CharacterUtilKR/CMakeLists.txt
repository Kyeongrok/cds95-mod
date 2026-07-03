add_library(CharacterUtilKR SHARED
    src/dllmain.c
    src/character.c
    src/ls12.c
)

target_link_libraries(CharacterUtilKR PRIVATE user32 gdi32)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(CharacterUtilKR PRIVATE /utf-8)
endif()

set_target_properties(CharacterUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
