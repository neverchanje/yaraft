#!/usr/bin/env bash

set -e

source deps_definition.sh

echo "Installing necessary dependencies for building yaraft..."

mkdir -p $TP_DIR
mkdir -p $TP_STAMP_DIR
cd $TP_DIR

install_if_necessary(){
    local depName=$1
    if [ ! -d $TP_DIR/$depName ]; then
        fetch_and_expand $depName.zip
    fi
    if [ ! -f $TP_STAMP_DIR/$depName ]; then
        $2
        make_stamp $depName
    fi
}

install_if_necessary $FMT_NAME build_fmtlib
install_if_necessary $PROTOBUF_NAME build_protobuf
install_if_necessary $GLOG_NAME build_glog
install_if_necessary $GFLAG_NAME build_gflag

if [ ! -d $GTEST_SOURCE ]; then
    fetch_and_expand ${GTEST_NAME}.zip
fi

echo "Dependencies installation of yaraft completed"