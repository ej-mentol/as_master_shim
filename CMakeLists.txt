cmake_minimum_required(VERSION 3.10)
project(as_master_shim)

set(CMAKE_CXX_STANDARD 17)

set(MMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/metamod-p")

if(EXISTS "${MMP_ROOT}/hlsdk/common/const.h")
    set(MMP_SDK "${MMP_ROOT}/hlsdk")
elseif(EXISTS "${MMP_ROOT}/include/hlsdk/common/const.h")
    set(MMP_SDK "${MMP_ROOT}/include/hlsdk")
else()
    message(FATAL_ERROR "HLSDK not found under thirdparty/metamod-p")
endif()

if(EXISTS "${MMP_ROOT}/metamod/meta_api.h")
    set(MMP_META "${MMP_ROOT}/metamod")
elseif(EXISTS "${MMP_ROOT}/include/metamod/meta_api.h")
    set(MMP_META "${MMP_ROOT}/include/metamod")
else()
    message(FATAL_ERROR "Metamod headers not found under thirdparty/metamod-p")
endif()

include_directories(SYSTEM ${MMP_SDK}/common)
include_directories(SYSTEM ${MMP_SDK}/dlls)
include_directories(SYSTEM ${MMP_SDK}/engine)
include_directories(SYSTEM ${MMP_SDK}/pm_shared)
include_directories(SYSTEM ${MMP_META})
include_directories(src)

if(WIN32)
    add_definitions(-DWIN32 -D_WINDOWS -D_CRT_SECURE_NO_WARNINGS)
else()
    add_definitions(-Dlinux)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -march=i686 -mincoming-stack-boundary=2 -fno-strict-aliasing")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
endif()

set(PROJECT_SRCS
    src/main.cpp
    src/patch.cpp
)

add_library(as_master_shim SHARED ${PROJECT_SRCS})

if(WIN32)
    target_link_libraries(as_master_shim psapi)
else()
    set_target_properties(as_master_shim PROPERTIES PREFIX "")
endif()
