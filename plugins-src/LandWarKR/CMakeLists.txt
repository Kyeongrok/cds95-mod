add_library(LandWarKR SHARED
    src/dllmain.c
    src/warwin.c
    src/landwar.c
)

target_link_libraries(LandWarKR PRIVATE minhook user32 gdi32)

target_compile_definitions(LandWarKR PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(LandWarKR PRIVATE /utf-8)
endif()

set_target_properties(LandWarKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
