#!/bin/sh

THRIFT_DEPS_DIR=$1
CMAKE_BUILD_DIR=$2
PROJECT_DIR=$(cd "$(dirname "$0")"; pwd)

echo ${THRIFT_DEPS_DIR}
echo ${CMAKE_BUILD_DIR}
echo ${PROJECT_DIR}

mkdir -p ${CMAKE_BUILD_DIR}/include/yaraft/thrift
${THRIFT_DEPS_DIR}/bin/thrift --out ${CMAKE_BUILD_DIR}/include/yaraft/thrift \
                              --gen cpp \
                              ${PROJECT_DIR}/include/yaraft/thrift/raft.thrift

sed -i '/cxxfunctional/d' ${CMAKE_BUILD_DIR}/include/yaraft/thrift/raft_types.h
