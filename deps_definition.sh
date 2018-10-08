#!/usr/bin/env bash

PROJECT_DIR=$(cd "$(dirname "$BASH_SOURCE")"; pwd)
BUILD_DIR=$PROJECT_DIR/build
TP_DIR=$PROJECT_DIR/third_parties
TP_BUILD_DIR=$BUILD_DIR/third_parties
TP_STAMP_DIR=$TP_BUILD_DIR/stamp

PROTOBUF_VERSION=2.6.1
PROTOBUF_NAME=protobuf-$PROTOBUF_VERSION
PROTOBUF_SOURCE=$TP_DIR/$PROTOBUF_NAME

GTEST_VERSION=1.8.0
GTEST_NAME=googletest-$GTEST_VERSION
GTEST_SOURCE=$TP_DIR/$GTEST_NAME

FMT_VERSION=5.2.1
FMT_NAME=fmt-$FMT_VERSION
FMT_SOURCE=$TP_DIR/$FMT_NAME

THRIFT_VERSION=0.9.3
THRIFT_NAME=thrift-$THRIFT_VERSION
THRIFT_SOURCE=$TP_DIR/$THRIFT_NAME

QINIU_CDN_URL_PREFIX=http://onnzg1pyx.bkt.clouddn.com

make_stamp() {
  touch $TP_STAMP_DIR/$1
}

fetch_and_expand() {
  local FILENAME=$1
  if [ -z "$FILENAME" ]; then
    echo "Error: Must specify file to fetch"
    exit 1
  fi

  TAR_CMD=tar
  if [[ "$OSTYPE" == "darwin"* ]] && which gtar &>/dev/null; then
    TAR_CMD=gtar
  fi

  FULL_URL="${QINIU_CDN_URL_PREFIX}/${FILENAME}"
  SUCCESS=0
  # Loop in case we encounter a corrupted archive and we need to re-download it.
  for attempt in 1 2; do
    if [ -r "$FILENAME" ]; then
      echo "Archive $FILENAME already exists. Not re-downloading archive."
    else
      echo "Fetching $FILENAME from $FULL_URL"
      wget "$FULL_URL"
    fi

    echo "Unpacking $FILENAME"
    if [[ "$FILENAME" =~ \.zip$ ]]; then
      if ! unzip -q "$FILENAME"; then
        echo "Error unzipping $FILENAME, removing file"
        rm "$FILENAME"
        continue
      fi
    elif [[ "$FILENAME" =~ \.(tar\.gz|tgz)$ ]]; then
      if ! $TAR_CMD xf "$FILENAME"; then
        echo "Error untarring $FILENAME, removing file"
        rm "$FILENAME"
        continue
      fi
    else
      echo "Error: unknown file format: $FILENAME"
      exit 1
    fi

    SUCCESS=1
    break
  done

  if [ $SUCCESS -ne 1 ]; then
    echo "Error: failed to fetch and unpack $FILENAME"
    exit 1
  fi

  # Allow for not removing previously-downloaded artifacts.
  # Useful on a low-bandwidth connection.
  if [ -z "$NO_REMOVE_THIRDPARTY_ARCHIVES" ]; then
    echo "Removing $FILENAME"
    rm $FILENAME
  fi
  echo
}

build_protobuf() {
  echo "Installing protobuf..."
  pushd ${PROTOBUF_SOURCE}
  autoreconf -ivf
  ./configure --prefix=${TP_BUILD_DIR}
  make -j4 && make install
  popd
}

build_fmtlib() {
  echo "Installing fmtlib..."
  pushd ${FMT_SOURCE}
  mkdir -p build && cd build
  cmake .. -DCMAKE_INSTALL_PREFIX=${TP_BUILD_DIR} -DFMT_TEST=false
  make -j4 && make install
  popd
}

build_thrift() {
  echo "Installing thrift..."
  pushd ${THRIFT_SOURCE}
  CMAKE_FLAGS="-DCMAKE_BUILD_TYPE=release\
    -DWITH_JAVA=OFF\
    -DWITH_PYTHON=OFF\
    -DWITH_C_GLIB=OFF\
    -DWITH_CPP=ON\
    -DBUILD_TESTING=OFF\
    -DBUILD_EXAMPLES=OFF\
    -DWITH_QT5=OFF\
    -DWITH_QT4=OFF\
    -DWITH_OPENSSL=OFF\
    -DBUILD_TUTORIALS=OFF\
    -DWITH_LIBEVENT=OFF\
    -DBUILD_COMPILER=ON\
    -DCMAKE_INSTALL_PREFIX=$TP_BUILD_DIR\
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON\
    -DWITH_SHARED_LIB=OFF"
  echo $CMAKE_FLAGS
  mkdir -p build && cd build
  cmake .. $CMAKE_FLAGS
  make -j8 && make install
  popd
}
