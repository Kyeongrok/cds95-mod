add_library(HotkeyUtilKR SHARED
    src/dllmain.c
    src/hotkey.c
    src/hkjson.c        # hotkeys.json 읽기. CharacterUtilKR 도 이 파일을 같이 빌드한다
    # 창은 다른 KR 창들과 같은 세피아 판이다 — 그리기 도구를 복사하지 않고 원본을 같이 빌드한다.
    ../CharacterUtilKR/src/uikit.c
)

target_include_directories(HotkeyUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common          # modmenu.h — 모드 창 등록부 약속
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src
)

# 후킹이 없어 minhook 은 안 쓴다 — 키는 WH_KEYBOARD(user32) 로 잡는다.
target_link_libraries(HotkeyUtilKR PRIVATE user32 gdi32)

target_compile_definitions(HotkeyUtilKR PRIVATE UNICODE _UNICODE)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(HotkeyUtilKR PRIVATE /utf-8)
endif()

set_target_properties(HotkeyUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll)가 CDS95Util 폴더에서 *.plugin 만 스캔한다.
)
