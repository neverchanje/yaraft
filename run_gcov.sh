#!/usr/bin/env bash

PROJECT_DIR=$(cd "$(dirname "$BASH_SOURCE")"; pwd)
GCOV_DIR=${PROJECT_DIR}/gcov-report
BUILD_DIR=${PROJECT_DIR}/cmake-build-debug

ENABLE_GCOV=True ./run_tests.sh

echo "Running gcovr to produce HTML code coverage report."
cd ${PROJECT_DIR}
mkdir -p ${GCOV_DIR}
gcovr --html --html-details -r ${PROJECT_DIR} --object-directory=${BUILD_DIR} \
      -e build/ -e third_parties/ -e ${BUILD_DIR} \
      -o ${GCOV_DIR}/index.html
