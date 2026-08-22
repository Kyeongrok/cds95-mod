add_library(LandWarKR SHARED
    src/dllmain.c
    src/warwin.c
    src/landwar.c
    # 창을 게임 껍데기로 입히는 조각들은 ButtonMakerKR 것을 그대로 같이 빌드한다
    # (복사하면 한쪽만 고쳐진다).
    ../ButtonMakerKR/src/gameskin.c
    ../ButtonMakerKR/src/band.c
    ../ButtonMakerKR/src/miscskin.c
    ../ButtonMakerKR/src/gamefont.c
    ../CharacterUtilKR/src/ls12.c
)

target_include_directories(LandWarKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common          # modmenu.h — 모드 창 등록부 약속
    ${CMAKE_CURRENT_SOURCE_DIR}/../ButtonMakerKR/src      # gameskin.h · band.h …
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src    # ls12.h · game_palette.h
)

target_link_libraries(LandWarKR PRIVATE minhook user32 gdi32)

target_compile_definitions(LandWarKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(LandWarKR PRIVATE /utf-8)
endif()

set_target_properties(LandWarKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
