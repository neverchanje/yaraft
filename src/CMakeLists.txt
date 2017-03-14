if (${BUILD_TEST})
    add_executable(memory_storage_test
            memory_storage_test.cc
            ${PROJECT_SOURCE_DIR}/pb/raftpb.pb.cc
            )
    target_link_libraries(memory_storage_test
            gtest gtest_main ${PROTOBUF_LIBRARY} ${GLOG_LIBRARY} ${FMT_LIBRARY})

    add_executable(util_test
            util_test.cc
            ${PROJECT_SOURCE_DIR}/pb/raftpb.pb.cc
            )
    target_link_libraries(util_test
            gtest gtest_main ${PROTOBUF_LIBRARY})

    add_executable(progress_test
            progress_test.cc
            )
    target_link_libraries(progress_test
            gtest gtest_main)

    add_executable(raft_log_test
            raft_log_test.cc
            ${PROJECT_SOURCE_DIR}/pb/raftpb.pb.cc
            )
    target_link_libraries(raft_log_test
            gtest gtest_main ${PROTOBUF_LIBRARY} ${FMT_LIBRARY} ${GLOG_LIBRARY})

    add_executable(raft_test
            raft_test.cc
            ${PROJECT_SOURCE_DIR}/pb/raftpb.pb.cc
            )
    target_link_libraries(raft_test
            gtest gtest_main ${PROTOBUF_LIBRARY} ${FMT_LIBRARY} ${GLOG_LIBRARY})

    add_executable(raft_paper_test
            raft_paper_test.cc
            ${PROJECT_SOURCE_DIR}/pb/raftpb.pb.cc
            )
    target_link_libraries(raft_paper_test
            gtest gtest_main ${PROTOBUF_LIBRARY} ${FMT_LIBRARY} ${GLOG_LIBRARY})

    add_executable(unstable_test
            unstable_test.cc
            ${PROJECT_SOURCE_DIR}/pb/raftpb.pb.cc
            )
    target_link_libraries(unstable_test
            gtest gtest_main ${PROTOBUF_LIBRARY} ${FMT_LIBRARY} ${GLOG_LIBRARY})
endif ()
