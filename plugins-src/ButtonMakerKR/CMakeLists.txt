add_library(ButtonMakerKR SHARED
    src/dllmain.c
    src/btnwin.c
    src/band.c
    src/miscskin.c
    src/gamefont.c
    # LS12 디코더와 PNG 입출력은 CharacterUtilKR 것을 그대로 같이 빌드한다
    # (복사하면 한쪽만 고쳐진다).
    ../CharacterUtilKR/src/ls12.c
    ../CharacterUtilKR/src/imgio.c
)

target_include_directories(ButtonMakerKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src   # ls12.h · imgio.h · game_palette.h
)

target_link_libraries(ButtonMakerKR PRIVATE user32 gdi32 comctl32 comdlg32)

target_compile_definitions(ButtonMakerKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(ButtonMakerKR PRIVATE /utf-8)
endif()

set_target_properties(ButtonMakerKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
