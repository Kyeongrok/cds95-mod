add_library(BookUtilKR SHARED
    src/dllmain.c
    src/bookdb.c
    src/bookwin.c
    # 힌트 이름·분류·상태는 HintUtilKR 것을 그대로 같이 빌드한다(복사하면 한쪽만 고쳐진다).
    ../HintUtilKR/src/hintdb.c
    # 도서관 유무(건물표)와 언어 이름표는 SkillUtilKR 것을 그대로 쓴다.
    ../SkillUtilKR/src/skilldb.c
)

target_include_directories(BookUtilKR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../HintUtilKR/src    # hintdb.h · hint_rows.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../SkillUtilKR/src   # skilldb.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../TradeUtilKR/src   # cities_data.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../WorldMapKR/src    # city_coords.h
)

target_link_libraries(BookUtilKR PRIVATE user32 gdi32 comctl32)

target_compile_definitions(BookUtilKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(BookUtilKR PRIVATE /utf-8)
endif()

set_target_properties(BookUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
