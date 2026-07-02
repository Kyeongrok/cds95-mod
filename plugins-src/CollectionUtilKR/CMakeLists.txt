add_library(CollectionUtilKR SHARED
    src/dllmain.c
    src/version.c
    src/hooks.c
)

target_link_libraries(CollectionUtilKR PRIVATE minhook version shlwapi)

set_target_properties(CollectionUtilKR PROPERTIES
    PREFIX ""
    SUFFIX ".plugin"   # 게임 로더(ddraw.dll = DDrawWrapper.plugin)가 CDS95Util 폴더에서 *.plugin 확장자만 스캔합니다.
)
