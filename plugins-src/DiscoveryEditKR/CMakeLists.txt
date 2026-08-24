add_library(DiscoveryEditKR SHARED
    src/dllmain.c
    src/dccoord.c
    src/dcwin.c
    # 이름·분류·발견 여부는 HintUtilKR 것을 그대로 같이 빌드한다(복사하면 한쪽만 고쳐진다).
    ../HintUtilKR/src/disc.c
    ../HintUtilKR/src/discinst.c
    ../HintUtilKR/src/hintdb.c
)

target_include_directories(DiscoveryEditKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common          # modmenu.h — 모드 창 등록부 약속
    ${CMAKE_CURRENT_SOURCE_DIR}/../HintUtilKR/src    # disc.h · discinst.h · hintdb.h
)

# 후킹이 없다 — 모듈 안의 표를 읽고, 좌표 넷만 VirtualProtect 로 잠깐 열어 고친다.
target_link_libraries(DiscoveryEditKR PRIVATE user32 gdi32 comctl32)

target_compile_definitions(DiscoveryEditKR PRIVATE UNICODE _UNICODE)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(DiscoveryEditKR PRIVATE /utf-8)
endif()

set_target_properties(DiscoveryEditKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll)가 CDS95Util 폴더에서 *.plugin 만 스캔한다.
)
