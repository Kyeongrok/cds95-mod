add_library(TavernInfoKR SHARED
    src/dllmain.c
    src/tavdb.c
    src/tavwin.c
)

target_include_directories(TavernInfoKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common            # modmenu.h — 모드 창 등록부 약속
    ${CMAKE_CURRENT_SOURCE_DIR}/../HintUtilKR/src    # hint_rows.h — kHints[186] 이름표
    ${CMAKE_CURRENT_SOURCE_DIR}/../TradeUtilKR/src   # cities_data.h — kCities[226] 이름표
)

# 후킹이 없다 — 모듈 안의 표를 읽고, 코드·도시 칸만 VirtualProtect 로 잠깐 열어 고친다.
target_link_libraries(TavernInfoKR PRIVATE user32 gdi32 comctl32)

target_compile_definitions(TavernInfoKR PRIVATE UNICODE _UNICODE)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(TavernInfoKR PRIVATE /utf-8)
endif()

set_target_properties(TavernInfoKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll)가 CDS95Util 폴더에서 *.plugin 만 스캔한다.
)
