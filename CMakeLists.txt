cmake_minimum_required(VERSION 2.8)
project(yaraft)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules")

set(CXX_FLAGS
        -Wall
        -Wold-style-cast #warn if C-style implicit-casting occurs
        -Wconversion
        )
set(CMAKE_CXX_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

set(DEPS_LIB_DIR build/third_parties/lib)
find_library(SILLY_LIBRARY silly_lite PATH ${DEPS_LIB_DIR})
find_library(GLOG_LIBRARY glog PATH ${DEPS_LIB_DIR})

# protobuf
find_library(PROTOBUF_LIBRARY protobuf PATH ${DEPS_LIB_DIR} NAME libprotobuf.a NO_DEFAULT_PATH)
find_library(PROTOC_LIBRARY protoc PATH ${DEPS_LIB_DIR} NO_DEFAULT_PATH)
message("Found ${PROTOBUF_LIBRARY}")
message("Found ${PROTOC_LIBRARY}")


option(BUILD_TEST OFF)
if(BUILD_TEST)
    enable_testing()
    add_subdirectory(third_parties/googletest/googletest)
endif()

include_directories(src)
include_directories(include)
include_directories(pb)
include_directories(build/third_parties/include)

add_subdirectory(src)
add_subdirectory(bin)

file(GLOB YARAFT_SOURCE "src/*.cc")

add_library(yaraft
    ${YARAFT_SOURCE}
    )
