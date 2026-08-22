add_library(ModWindowKR SHARED
    src/dllmain.c
    src/menu.c
    src/modwin.c
    # 창과 단추는 다른 KR 플러그인 것을 복사하지 않고 그대로 같이 빌드한다.
    ../CharacterUtilKR/src/uikit.c
    ../ButtonMakerKR/src/gameskin.c
    ../ButtonMakerKR/src/band.c
    ../ButtonMakerKR/src/miscskin.c
    ../ButtonMakerKR/src/gamefont.c
    ../CharacterUtilKR/src/ls12.c
)

target_include_directories(ModWindowKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common          # modmenu.h — 모드 창 등록부 약속
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src   # uikit.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ButtonMakerKR/src     # gameskin.h · band.h …
)

target_link_libraries(ModWindowKR PRIVATE user32 gdi32)

target_compile_definitions(ModWindowKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(ModWindowKR PRIVATE /utf-8)
endif()

set_target_properties(ModWindowKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
