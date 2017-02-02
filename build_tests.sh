cd build
cmake .. -DBUILD_TEST=ON
make -j4 memory_storage_test util_test
