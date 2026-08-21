add_library(WindArrowKR SHARED
    src/dllmain.c
    src/hook.c
    src/menu.c
    src/arrow.c
    src/wind.c
)

target_link_libraries(WindArrowKR PRIVATE minhook user32)

# 소스에 한국어(UTF-8) 주석과 와이드 문자열이 있으므로 MSVC 에 UTF-8 소스로 알려준다.
if(MSVC)
    target_compile_options(WindArrowKR PRIVATE /utf-8)
endif()

set_target_properties(WindArrowKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 로더(ddraw.dll)가 CDS95Util 에서 *.plugin 만 스캔한다
)
