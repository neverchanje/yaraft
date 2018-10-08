
add_library(yaraft
        memory_storage.cc
        stderr_logger.cc
        util.cc
        rand.cc
        progress.cc
        raft.cc
        read_only.cc
        conf.cc
        raw_node.cc
        ${CMAKE_BINARY_DIR}/include/yaraft/thrift/raft_constants.cpp
        ${CMAKE_BINARY_DIR}/include/yaraft/thrift/raft_types.cpp
        )
target_link_libraries(yaraft
        ${FMT_LIBRARY}
        )

file(GLOB YARAFT_HEADERS "${PROJECT_SOURCE_DIR}/include/*.h")
install(TARGETS yaraft DESTINATION lib)
install(FILES ${YARAFT_HEADERS} DESTINATION include/yaraft)

if (${BUILD_TEST})
    add_executable(yaraft_test
            memory_storage_test.cc
            util_test.cc
            inflights_test.cc
            progress_test.cc
            unstable_test.cc
            raft_log_test.cc
            ready_test.cc
            raft_paper_test.cc
            raft_snap_test.cc
            raft_flow_control_test.cc
            raft_test.cc
            test_utils.cc
            raw_node_test.cc
            )
    target_link_libraries(yaraft_test
            ${FMT_LIBRARY}
            ${PROTOBUF_STATIC_LIBRARY}
            ${THRIFT_LIBRARIES}
            yaraft
            gtest
            gtest_main
            )
endif ()
