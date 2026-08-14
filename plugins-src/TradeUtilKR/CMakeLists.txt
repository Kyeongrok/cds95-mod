add_library(TradeUtilKR SHARED
    src/dllmain.c
    src/trade.c
    # 교역품 그림(ITEM.CDS) — CharacterUtilKR 것을 그대로 같이 빌드한다.
    # 복사해 오면 한쪽만 고쳐지는 사고가 나므로, 아래 include 경로와 함께 원본을 참조한다.
    ../CharacterUtilKR/src/itempic.c
    ../CharacterUtilKR/src/ls12.c
    # 도시 그림(CITYCG.CDS) — CityPicKR 것을 같은 이유로 참조한다.
    # 시세 일람은 CityCg_Draw 만 쓰지만, citycg.c 의 내보내기/넣기가 quant·imgio 를 부르므로 같이 건다
    # (imgio.c 는 gdiplus.dll 을 실행 중에 부르니 링크할 라이브러리는 늘지 않는다).
    ../CityPicKR/src/citycg.c
    ../CityPicKR/src/cityfrm.c
    ../CityPicKR/src/quant.c
    ../CharacterUtilKR/src/imgio.c
)

target_include_directories(TradeUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../CityPicKR/src
)

target_link_libraries(TradeUtilKR PRIVATE user32 gdi32 comctl32)

# 소스에 한국어(UTF-8) 와이드 문자열 리터럴이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(TradeUtilKR PRIVATE /utf-8)
endif()

set_target_properties(TradeUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
