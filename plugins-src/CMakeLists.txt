cmake_minimum_required(VERSION 3.20)
project(cds95-plugins C)

# CDS95.exe는 32비트(PE32) 프로세스이므로 플러그인 DLL도 반드시 x86으로 빌드해야 합니다.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "CDS95.exe는 32비트 프로세스입니다. Visual Studio 생성 시 -A Win32 를 지정하세요. (예: cmake -A Win32 ..)")
endif()

# MinHook 소스 위치. 아래 명령으로 서브모듈을 받아오세요:
#   git submodule add https://github.com/TsudaKageyu/minhook.git plugins-src/third_party/minhook
set(MINHOOK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook" CACHE PATH "MinHook source root")

if(NOT EXISTS "${MINHOOK_DIR}/include/MinHook.h")
    message(FATAL_ERROR
        "MinHook 소스를 찾을 수 없습니다: ${MINHOOK_DIR}\n"
        "다음 명령으로 받아오세요:\n"
        "  git submodule add https://github.com/TsudaKageyu/minhook.git plugins-src/third_party/minhook\n"
        "  git submodule update --init")
endif()

add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC
    ${MINHOOK_DIR}/include
    ${MINHOOK_DIR}/src
)

add_subdirectory(CollectionUtilKR)
add_subdirectory(HotelUtilKR)
add_subdirectory(TradeUtilKR)
