cmake_minimum_required(VERSION 2.8)
project(yaraft)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")

set(CXX_FLAGS
        -Wall
        -Wold-style-cast #warn if C-style implicit-casting occurs
        -Wconversion
        -g
        )
set(CMAKE_CXX_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
message("-- CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")

option(BUILD_TEST ON)

set(THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/third_parties)

# Look in thirdparty prefix paths before anywhere else for system dependencies.
set(CMAKE_PREFIX_PATH ${THIRDPARTY_DIR} ${CMAKE_PREFIX_PATH})

## Protobuf
find_package(Protobuf REQUIRED)
include_directories(SYSTEM ${PROTOBUF_INCLUDE_DIR})

## GLog
find_package(GLog REQUIRED)
include_directories(SYSTEM ${GLOG_INCLUDE_DIR})

find_library(SILLY_LIBRARY silly_lite PATH ${DEPS_LIB_DIR})
message("-- Found ${SILLY_LIBRARY}")

find_library(FMT_LIBRARY fmt PATH ${DEPS_LIB_DIR})
message("-- Found ${FMT_LIBRARY}")

include_directories(src)
include_directories(include)
include_directories(build/third_parties/include)
include_directories(third_parties/googletest-1.8.0/googletest/include)

add_subdirectory(src)
add_subdirectory(third_parties/googletest-1.8.0/googletest)