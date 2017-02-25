#!/usr/bin/env bash

set -e

DEPS_PREFIX=`pwd`/build/third_parties
FLAG_PREFIX=`pwd`/build/third_parties/have_built
DEPS_DIR=`pwd`/third_parties
BUILD_DIR=`pwd`/build

echo "Installing googletest..."
if [ ! -f "${FLAG_PREFIX}/googletest_1_8_0" ] \
    || [ ! -d "${DEPS_DIR}/googletest" ]; then
    wget -O ${DEPS_DIR}/googletest.zip https://github.com/google/googletest/archive/release-1.8.0.zip
    unzip -q -o ${DEPS_DIR}/googletest.zip -d ${DEPS_DIR}
    mv ${DEPS_DIR}/googletest-release-1.8.0 ${DEPS_DIR}/googletest
    touch "${FLAG_PREFIX}/googletest_1_8_0"
fi

cd ${BUILD_DIR}
cmake .. -DBUILD_TEST=ON
make -j4
