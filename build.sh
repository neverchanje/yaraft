DO_TEST=0

while getopts "t" arg
do
        case $arg in
             t)
				DO_TEST=1
                ;;
             ?)
                echo "unknown argument $arg"
                exit 1
                ;;
        esac
done

DEPS_PREFIX=`pwd`/build/third_parties
FLAG_PREFIX=`pwd`/build/third_parties/have_built
DEPS_DIR=`pwd`/third_parties
BUILD_DIR=`pwd`/build
PROTO_FILES_DIR=`pwd`/pb

mkdir -p ${FLAG_PREFIX}

# glog
if [ ! -f "${FLAG_PREFIX}/glog_0_3_4" ] \
	|| [ ! -d "${DEPS_DIR}/glog" ]; then
	cd ${DEPS_DIR}/glog
	./configure --prefix=${DEPS_PREFIX} --disable-shared
	make -j4 && make install
	touch "${FLAG_PREFIX}/glog_0_3_4"
fi

# silly
if [ ! -f "${FLAG_PREFIX}/silly" ] \
	|| [ ! -d "${DEPS_DIR}/silly" ]; then
	cd ${DEPS_DIR}/silly
	mkdir -p build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX} -DLITE_VERSION=true
	make && make install
	touch "${FLAG_PREFIX}/silly"
fi

# protobuf
if [ ! -f "${FLAG_PREFIX}/protobuf_2_7_0" ] \
	|| [ ! -d "${DEPS_DIR}/protobuf" ]; then
	cd ${DEPS_DIR}/protobuf
    git checkout origin/2.7.0
    ./autogen.sh
    ./configure --prefix=${DEPS_PREFIX} --disable-shared
	make -j4 && make install
	touch "${FLAG_PREFIX}/protobuf_2_7_0"
fi

# googletest
if [ ${DO_TEST} -eq 1 ]; then
    if [ ! -f "${FLAG_PREFIX}/googletest_1_8_0" ] \
        || [ ! -d "${DEPS_DIR}/googletest" ]; then
        cd ${DEPS_DIR}/googletest
        mkdir -p build && cd build
        cmake .. -DCMAKE_INSTALL_PREFIX=${DEPS_PREFIX}
        make -j4 && make install
        touch "${FLAG_PREFIX}/googletest_1_8_0"
    fi
fi

cd ${BUILD_DIR}
if [ ${DO_TEST} -eq 1 ]; then
    cmake .. -DBUILD_TEST=ON
else
    cmake ..
fi


# compile protos
echo "Generating proto files"
${DEPS_PREFIX}/bin/protoc --proto_path=${PROTO_FILES_DIR} ${PROTO_FILES_DIR}/raftpb.proto --cpp_out=${PROTO_FILES_DIR}

make -j4


