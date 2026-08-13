add_library(CityPicKR SHARED
    src/dllmain.c
    src/menu.c
    src/picwin.c
    src/citycg.c
    src/quant.c
    ../CharacterUtilKR/src/ls12.c
    ../CharacterUtilKR/src/uikit.c
    ../CharacterUtilKR/src/imgio.c
)

# comdlg32: PNG 파일 고르는 대화상자. gdiplus 는 링크하지 않고 실행 중에 부른다
# (imgio.c 참고 — gdiplus.dll 이 없는 환경에서도 플러그인 자체는 떠야 한다).
target_link_libraries(CityPicKR PRIVATE user32 gdi32 comdlg32)

# LS12 디코더·공용 색표·위젯(uikit)은 CharacterUtilKR, 도시 이름표(kCities)는 TradeUtilKR 쪽 것을
# 그대로 쓴다. 복사해 두면 한쪽만 고쳐지는 사고가 나므로 소스를 참조한다(WorldMapKR 과 같다).
target_include_directories(CityPicKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../TradeUtilKR/src
)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(CityPicKR PRIVATE /utf-8)
endif()

set_target_properties(CityPicKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 게임 로더(ddraw.dll)가 CDS95Util 폴더에서 *.plugin 확장자만 스캔합니다.
)
