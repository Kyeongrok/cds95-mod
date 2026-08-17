add_library(MarketUtilKR SHARED
    src/dllmain.c
    src/market.c
    src/marketdb.c
    # 창과 그림은 다른 KR 플러그인 것을 복사하지 않고 그대로 같이 빌드한다.
    ../CharacterUtilKR/src/uikit.c
    # 단추를 게임 껍데기로 그리는 조각 — ButtonMakerKR 것을 그대로 같이 빌드한다
    ../ButtonMakerKR/src/gameskin.c
    ../ButtonMakerKR/src/band.c
    ../ButtonMakerKR/src/miscskin.c
    ../ButtonMakerKR/src/gamefont.c
    ../CharacterUtilKR/src/itempic.c
    ../CharacterUtilKR/src/ls12.c
)

target_include_directories(MarketUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../TradeUtilKR/src   # goods_names.h · cities_data.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../ButtonMakerKR/src # gameskin.h · band.h …
)

target_link_libraries(MarketUtilKR PRIVATE minhook user32 gdi32)

target_compile_definitions(MarketUtilKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(MarketUtilKR PRIVATE /utf-8)
endif()

set_target_properties(MarketUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
