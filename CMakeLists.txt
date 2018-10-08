# Copyright 2017 Wu Tao
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


cmake_minimum_required(VERSION 2.8.12)
project(yaraft)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # require at least gcc 4.8
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC is too old, please install a newer version supporting C++11")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # require at least clang 3.3
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
        message(FATAL_ERROR "Clang is too old, please install a newer version supporting C++11")
    endif()
else()
    message(WARNING "You are using an unsupported compiler! Compilation has only been tested with Clang and GCC.")
endif()


if(CMAKE_VERSION VERSION_LESS "3.1.3")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()


set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules")
include(cotire) # use cotire to reduce compile time
include(CompilerInfo) # to determine compiler info

set(CXX_FLAGS
        -Wall
        -Wold-style-cast #warn if C-style implicit-casting occurs
        -Wconversion
        -g
        )
set(CMAKE_CXX_FLAGS_DEBUG "-O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
message("-- CMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")

# use customized thirdparty path if you want to build third-parties on your own.
if (${THIRDPARTY_DIR})
else()
    # download and build thirdparty by running install_deps_if_necessary.sh
    execute_process(COMMAND ${CMAKE_SOURCE_DIR}/install_deps_if_necessary.sh RESULT_VARIABLE THIRDPARTY_SCRIPT_RESULT)
    if (NOT (${THIRDPARTY_SCRIPT_RESULT} EQUAL 0))
        message(FATAL_ERROR "Thirdparty was built unsuccessfully, terminating.")
    endif ()
    set(THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/third_parties)
endif ()
# Look in thirdparty prefix paths before anywhere else for system dependencies.
set(CMAKE_PREFIX_PATH ${THIRDPARTY_DIR} ${CMAKE_PREFIX_PATH})
include_directories(${THIRDPARTY_DIR}/include)


# use thrift as our default IDL.
option(BUILD_THRIFT "Use Thrift as IDL" ON)
option(BUILD_PROTO "Use Protobuf as IDL" OFF)
if (${BUILD_PROTO})
    set(BUILD_THRIFT OFF)
    find_package(Protobuf REQUIRED)
    include_directories(${PROTOBUF_INCLUDE_DIR})
    message("-- Found ${PROTOBUF_STATIC_LIBRARY}")
    # compile proto files and install it under build/include
    execute_process(COMMAND ${CMAKE_SOURCE_DIR}/compile_proto.sh ${THIRDPARTY_DIR} ${CMAKE_CURRENT_BINARY_DIR})
    message("${CMAKE_SOURCE_DIR}/compile_proto.sh ${THIRDPARTY_DIR} ${CMAKE_CURRENT_BINARY_DIR}")
    include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
    add_definitions(-DIDL_TYPE_PROTO)
endif ()
if (${BUILD_THRIFT})
    find_package(Thrift REQUIRED)
    include_directories(${THRIFT_INCLUDE_DIR})
    message("-- Found ${THRIFT_LIBRARIES} ${THRIFT_VERSION_STRING}")
    # compile thrift files and install it under build/include
    message("${CMAKE_SOURCE_DIR}/compile_thrift.sh ${THIRDPARTY_DIR} ${CMAKE_CURRENT_BINARY_DIR}")
    execute_process(COMMAND ${CMAKE_SOURCE_DIR}/compile_thrift.sh ${THIRDPARTY_DIR} ${CMAKE_CURRENT_BINARY_DIR})
    include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)
    add_definitions(-DIDL_TYPE_THRIFT)
endif ()


# find fmtlib
find_library(FMT_LIBRARY fmt PATH ${THIRDPARTY_DIR})
message("-- Found ${FMT_LIBRARY}")


option(BUILD_TEST "Whether to build unit tests" OFF)
if (${BUILD_TEST})
    include_directories(third_parties/googletest-1.8.0/googletest/include)
    add_subdirectory(third_parties/googletest-1.8.0/googletest)
endif ()


# Always generate the compilation database file (compile_commands.json) for use
# with various development tools, such as IWYU and Vim's YouCompleteMe plugin.
# See http://clang.llvm.org/docs/JSONCompilationDatabase.html
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Code Coverage
if(ENABLE_GCOV)
    find_program(GCOV_PATH gcov)
    find_program(GCOVR_PATH gcovr PATHS)

    if(NOT GCOV_PATH)
        message(FATAL_ERROR "gcov not found! Aborting...")
    endif() # NOT GCOV_PATH

    if(NOT GCOVR_PATH)
        message(FATAL_ERROR "gcovr not found! Aborting...")
    endif() # NOT GCOVR_PATH

    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Code coverage results with an optimised (non-Debug) build may be misleading")
    endif() # NOT CMAKE_BUILD_TYPE STREQUAL "Debug"

    set(COVERAGE_COMPILER_FLAGS "-g -O0 --coverage -fprofile-arcs -ftest-coverage" CACHE INTERNAL "")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_COMPILER_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_COMPILER_FLAGS}")
    message(STATUS "Appending code coverage compiler flags: ${COVERAGE_COMPILER_FLAGS}")

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lgcov")
endif()

include_directories(include)
add_subdirectory(src)
