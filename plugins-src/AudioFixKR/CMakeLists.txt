add_library(AudioFixKR SHARED
    src/dllmain.c
)

target_link_libraries(AudioFixKR PRIVATE minhook)

target_compile_options(AudioFixKR PRIVATE /utf-8)

set_target_properties(AudioFixKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"
)
