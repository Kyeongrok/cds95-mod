add_library(ModUtilKR SHARED
    src/dllmain.c
    src/mod.c
    src/playermod.c
    # 플레이어 수정 창이 쓰는 조각들 — 다른 플러그인 것을 그대로 같이 빌드한다
    # (복사하면 한쪽만 고쳐진다).
    ../CharacterUtilKR/src/uikit.c        # 세피아 판과 위젯
    ../CharacterUtilKR/src/inventory.c    # 소지금
    ../CharacterUtilKR/src/livechar.c     # 명성 · 주인공 레코드
    ../ButtonMakerKR/src/gameskin.c       # 단추를 게임 띠로
    ../ButtonMakerKR/src/band.c
    ../ButtonMakerKR/src/miscskin.c
    ../ButtonMakerKR/src/gamefont.c
    ../CharacterUtilKR/src/ls12.c
)

target_include_directories(ModUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common          # modmenu.h — 모드 창 등록부 약속
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src   # uikit.h · inventory.h · livechar.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ButtonMakerKR/src     # gameskin.h · band.h …
)

target_link_libraries(ModUtilKR PRIVATE user32 gdi32 comctl32 shell32)

# ListView 매크로를 와이드로 고정(정의 안 하면 ANSI 로 잡혀 한글이 깨진다).
target_compile_definitions(ModUtilKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(ModUtilKR PRIVATE /utf-8)
endif()

set_target_properties(ModUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
