#!/usr/bin/env bash

set -e

source deps_definition.sh

echo "Installing necessary dependencies for building..."

mkdir -p $TP_DIR
mkdir -p $TP_BUILD_DIR
cd $TP_DIR

install_deps_if_necessary() {
    if [ ! -d $GLOG_SOURCE ]; then
        fetch_and_expand ${GLOG_NAME}.zip
        build_glog
    fi

    if [ ! -d $FMT_SOURCE ]; then
        fetch_and_expand ${FMT_NAME}.zip
        build_fmtlib
    fi

    if [ ! -d $PROTOBUF_SOURCE ]; then
        fetch_and_expand ${PROTOBUF_NAME}.zip
        build_protobuf
    fi

    if [ ! -d $SILLY_SOURCE ]; then
        fetch_and_expand ${SILLY_NAME}.tar.gz
        build_silly
    fi

    if [ ! -d $GTEST_SOURCE ]; then
        fetch_and_expand ${GTEST_NAME}.zip
    fi
}