add_library(TradeUtilKR SHARED
    src/dllmain.c
    src/trade.c
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
