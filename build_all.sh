#!/usr/bin/env bash

# Exit if error happens.
set -e

source deps_definition.sh
source install_deps_if_neccessary.sh

install_deps_if_necessary

echo "Installing yaraft"

cd ${BUILD_DIR}
cmake ..

# compile protos
echo "Generating proto files"
${TP_BUILD_DIR}/bin/protoc --proto_path=${PROTO_FILES_DIR} ${PROTO_FILES_DIR}/raftpb.proto --cpp_out=${PROTO_FILES_DIR}

make -j4
