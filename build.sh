#!/usr/bin/env bash

# Exit if error happens.
set -e

DEPS_PREFIX=`pwd`/build/third_parties
FLAG_PREFIX=`pwd`/build/third_parties/have_built
DEPS_DIR=`pwd`/third_parties
BUILD_DIR=`pwd`/build
PROTO_FILES_DIR=`pwd`/pb

mkdir -p ${FLAG_PREFIX}
mkdir -p ${DEPS_DIR}

echo "Installing necessary dependencies for building..."
sudo apt-get -y install autoconf automake libtool curl make g++ unzip

echo "Installing glog..."
if [ ! -f "${FLAG_PREFIX}/glog_0_3_4" ] \
	|| [ ! -d "${DEPS_DIR}/glog-0.3.4" ]; then
    wget -O ${DEPS_DIR}/glog.zip https://github.com/google/glog/archive/v0.3.4.zip
    unzip -q -o ${DEPS_DIR}/glog.zip -d ${DEPS_DIR}
	cd ${DEPS_DIR}/glog-0.3.4
	./configure --prefix=${DEPS_PREFIX} --disable-shared
	make -j4 && make install
	touch "${FLAG_PREFIX}/glog_0_3_4"
fi

echo "Installing silly..."
if [ ! -f "${FLAG_PREFIX}/silly" ] \
	|| [ ! -d "${DEPS_DIR}/silly-master" ]; then
	wget -O ${DEPS_DIR}/silly.zip https://codeload.github.com/IppClub/silly/zip/master
	unzip -q -o ${DEPS_DIR}/silly.zip -d ${DEPS_DIR}
	cd ${DEPS_DIR}/silly-master
	mkdir -p build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX} -DLITE_VERSION=true
	make && make install
	touch "${FLAG_PREFIX}/silly"
fi

echo "Installing protobuf..."
if [ ! -f "${FLAG_PREFIX}/protobuf_2_6_1" ] \
	|| [ ! -d "${DEPS_DIR}/protobuf-2.6.1" ]; then
    wget -O ${DEPS_DIR}/protobuf.tar.gz https://github.com/google/protobuf/releases/download/v2.6.1/protobuf-2.6.1.tar.gz
    tar zxf ${DEPS_DIR}/protobuf.tar.gz -C ${DEPS_DIR}
	cd ${DEPS_DIR}/protobuf-2.6.1
	autoreconf -ivf
    ./configure --prefix=${DEPS_PREFIX} --disable-shared
	make -j4 && make install
	touch "${FLAG_PREFIX}/protobuf_2_6_1"
fi

echo "Installing fmt..."
if [ ! -f "${FLAG_PREFIX}/fmt_3_0_1" ] \
	|| [ ! -d "${DEPS_DIR}/fmt-3.0.1" ]; then
	wget -O ${DEPS_DIR}/fmt.zip https://github.com/fmtlib/fmt/releases/download/3.0.1/fmt-3.0.1.zip
	unzip -q -o ${DEPS_DIR}/fmt.zip -d ${DEPS_DIR}
	cd ${DEPS_DIR}/fmt-3.0.1
	mkdir -p build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX}
	make -j4 && make install
	touch "${FLAG_PREFIX}/fmt_3_0_1"
fi

echo "Installing yaraft"

cd ${BUILD_DIR}
cmake ..

# compile protos
echo "Generating proto files"
${DEPS_PREFIX}/bin/protoc --proto_path=${PROTO_FILES_DIR} ${PROTO_FILES_DIR}/raftpb.proto --cpp_out=${PROTO_FILES_DIR}

make -j4
