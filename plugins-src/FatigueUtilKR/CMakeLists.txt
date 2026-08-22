add_library(FatigueUtilKR SHARED
    src/dllmain.c
    src/fatigue.c
)

# 후킹이 없어 minhook 은 안 쓴다 — 값 하나를 읽고 쓰는 창이 전부다.
target_include_directories(FatigueUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common          # modmenu.h — 모드 창 등록부 약속
)

target_link_libraries(FatigueUtilKR PRIVATE user32 gdi32)

target_compile_definitions(FatigueUtilKR PRIVATE UNICODE _UNICODE)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(FatigueUtilKR PRIVATE /utf-8)
endif()

set_target_properties(FatigueUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll)가 CDS95Util 폴더에서 *.plugin 만 스캔한다.
)
