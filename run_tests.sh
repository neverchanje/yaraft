#!/usr/bin/env bash

PROJECT_DIR=$(cd "$(dirname "$BASH_SOURCE")"; pwd)
BUILD_DIR=${PROJECT_DIR}/cmake-build-debug

# Environment variables
# - BUILD
# - STANDARD
# - ENABLE_GCOV

if [ -z "${BUILD}" ]
then
    BUILD="Debug"
    echo "BUILD=Debug"
else
    echo "BUILD=${BUILD}"
fi

if [ -z "${STANDARD}" ]
then
    STANDARD=11
    echo "STANDARD=11"
else
    echo "STANDARD=${STANDARD}"
fi

if [ -z "${ENABLE_GCOV}" ]
then
    ENABLE_GCOV=false
    echo "ENABLE_GCOV=FALSE"
else
    echo "ENABLE_GCOV=${ENABLE_GCOV}"
fi

cd ${PROJECT_DIR}
rm -rf ${BUILD_DIR}
mkdir -p ${BUILD_DIR}

echo "Building yaraft_test"
cd ${BUILD_DIR}
cmake .. -DENABLE_GCOV=${ENABLE_GCOV} -DCMAKE_BUILD_TYPE=${BUILD} -DCMAKE_CXX_STANDARD=${STANDARD} -DBUILD_TEST=On
make -j4 yaraft_test
cd src
./yaraft_test
if [ $? -ne 0 ]
then
    echo "ERROR: running tests failed"
    exit 1
else
    echo "Running tests succeed"
fi
