add_library(ButtonMakerKR SHARED
    src/dllmain.c
    src/btnwin.c
    src/band.c
    src/miscskin.c
    src/gamefont.c
    src/gameskin.c
    # LS12 디코더와 PNG 입출력은 CharacterUtilKR 것을 그대로 같이 빌드한다
    # (복사하면 한쪽만 고쳐진다).
    ../CharacterUtilKR/src/ls12.c
    ../CharacterUtilKR/src/imgio.c
)

target_include_directories(ButtonMakerKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common          # modmenu.h — 모드 창 등록부 약속
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src   # ls12.h · imgio.h · game_palette.h
)

# comdlg32: PNG 저장 대화상자. gdiplus 는 링크하지 않고 실행 중에 부른다(imgio.c).
# comctl32 는 안 쓴다 — 표준 컨트롤(COMBOBOX/EDIT/BUTTON/STATIC)만 쓰므로 걸지 않는다.
target_link_libraries(ButtonMakerKR PRIVATE user32 gdi32 comdlg32)

target_compile_definitions(ButtonMakerKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(ButtonMakerKR PRIVATE /utf-8)
endif()

set_target_properties(ButtonMakerKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
