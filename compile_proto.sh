#!/bin/bash

PROTO_DEPS_DIR=$1
BUILD_DIR=$2
PROJECT_DIR=$(cd "$(dirname "$BASH_SOURCE")"; pwd)
PROTO_DIR=${PROJECT_DIR}/include/pb

mkdir -p ${BUILD_DIR}/include
${PROTO_DEPS_DIR}/bin/protoc --proto_path=${PROTO_DEPS_DIR}/include --proto_path=${PROJECT_DIR}/include \
 --cpp_out=${BUILD_DIR}/include ${PROJECT_DIR}/include/yaraft/pb/raft.proto
