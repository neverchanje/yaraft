#!/usr/bin/env bash

BUILD_DIR=`pwd`/build
TP_DIR="`pwd`/third_parties"
TP_BUILD_DIR="$BUILD_DIR/third_parties"
TP_STAMP_DIR="$TP_BUILD_DIR/stamp"
PROTO_FILES_DIR=`pwd`/pb

PROTOBUF_VERSION=2.6.1
PROTOBUF_NAME=protobuf-$PROTOBUF_VERSION
PROTOBUF_SOURCE=$TP_DIR/$PROTOBUF_NAME

GLOG_VERSION=0.3.4
GLOG_NAME=glog-$GLOG_VERSION
GLOG_SOURCE=$TP_DIR/$GLOG_NAME

GTEST_VERSION=1.8.0
GTEST_NAME=googletest-$GTEST_VERSION
GTEST_SOURCE=$TP_DIR/$GTEST_NAME

FMT_VERSION=3.0.1
FMT_NAME=fmt-$FMT_VERSION
FMT_SOURCE=$TP_DIR/$FMT_NAME

SILLY_VERSION=0.0.1-alpha-1
SILLY_NAME=silly-$SILLY_VERSION
SILLY_SOURCE=$TP_DIR/$SILLY_NAME

GFLAG_VERSION=2.2.0
GFLAG_NAME=gflags-$GFLAG_VERSION
GFLAG_SOURCE=$TP_DIR/$GFLAG_NAME

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

build_glog() {
  echo "Installing glog..."
  pushd ${GLOG_SOURCE}
	./configure --prefix=${TP_BUILD_DIR} --disable-shared
	make -j4 && make install
	popd
}

build_silly() {
  echo "Installing silly..."
  pushd ${SILLY_SOURCE}
	mkdir -p build && cd build
  cmake .. -DCMAKE_INSTALL_PREFIX=${TP_BUILD_DIR} -DLITE_VERSION=true
	make && make install
	popd
}

build_protobuf() {
  echo "Installing protobuf..."
  pushd ${PROTOBUF_SOURCE}
	autoreconf -ivf
  ./configure --prefix=${TP_BUILD_DIR} --disable-shared
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

build_gflag() {
  mkdir -p $GFLAG_SOURCE/build
  pushd $GFLAG_SOURCE/build
  rm -rf CMakeCache.txt CMakeFiles/
  cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POSITION_INDEPENDENT_CODE=On \
    -DCMAKE_INSTALL_PREFIX=$TP_BUILD_DIR \
    -DBUILD_SHARED_LIBS=On \
    -DBUILD_STATIC_LIBS=On \
    -DREGISTER_INSTALL_PREFIX=Off \
    $GFLAG_SOURCE
  make -j8 install
  popd
}