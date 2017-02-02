
if(BUILD_TEST)
    add_executable(memory_storage_test
        memory_storage_test.cc
        ${PROJECT_SOURCE_DIR}/pb/raftpb.pb.cc
        )
    target_link_libraries(memory_storage_test
        gtest gtest_main ${PROTOBUF_LIBRARY} ${GLOG_LIBRARY})

    add_executable(util_test
        util_test.cc
        ${PROJECT_SOURCE_DIR}/pb/raftpb.pb.cc
        )
    target_link_libraries(util_test
        gtest gtest_main ${PROTOBUF_LIBRARY})
endif()
