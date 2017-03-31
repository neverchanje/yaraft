set(YARAFT_TEST_LINK_LIBS
        gtest gtest_main
        ${PROTOBUF_STATIC_LIBRARY}
        ${FMT_LIBRARY}
        ${GLOG_STATIC_LIB})

function(ADD_YARAFT_TEST TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cc ${PROJECT_SOURCE_DIR}/pb/raftpb.pb.cc)
    target_link_libraries(${TEST_NAME} ${YARAFT_TEST_LINK_LIBS})
endfunction()

ADD_YARAFT_TEST(memory_storage_test)

ADD_YARAFT_TEST(util_test)

ADD_YARAFT_TEST(progress_test)

ADD_YARAFT_TEST(raft_log_test)

ADD_YARAFT_TEST(raft_test)

ADD_YARAFT_TEST(raft_paper_test)

ADD_YARAFT_TEST(unstable_test)