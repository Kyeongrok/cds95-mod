add_library(ShipInfoKR SHARED
    src/dllmain.c
    src/hulldb.c
    src/shipstil.c
    src/shipwin.c
    # LS12 디코더는 CharacterUtilKR 것을 그대로 같이 빌드한다(복사하면 한쪽만 고쳐진다).
    ../CharacterUtilKR/src/ls12.c
)

target_include_directories(ShipInfoKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../common          # modmenu.h — 모드 창 등록부 약속
    ${CMAKE_CURRENT_SOURCE_DIR}/../CharacterUtilKR/src   # ls12.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../TradeUtilKR/src       # cities_data.h
)

target_link_libraries(ShipInfoKR PRIVATE user32 gdi32 comctl32)

target_compile_definitions(ShipInfoKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(ShipInfoKR PRIVATE /utf-8)
endif()

set_target_properties(ShipInfoKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
