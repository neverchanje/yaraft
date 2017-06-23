set(YARAFT_TEST_LINK_LIBS
        gtest gtest_main
        ${PROTOBUF_STATIC_LIBRARY}
        ${FMT_LIBRARY}
        ${GLOG_STATIC_LIB})

set(YARAFT_SOURCE_DIR ${PROJECT_SOURCE_DIR}/src)
add_library(yaraft
        ${YARAFT_SOURCE_DIR}/memory_storage.cc
        ${YARAFT_SOURCE_DIR}/pb_utils.cc
        ${YARAFT_SOURCE_DIR}/raw_node.cc
        ${YARAFT_SOURCE_DIR}/status.cc
        ${YARAFT_SOURCE_DIR}/yaraft/pb/raftpb.pb.cc)
target_link_libraries(yaraft ${YARAFT_TEST_LINK_LIBS})

file(GLOB LIBYARAFT_INSTALL_HEADERS "${PROJECT_SOURCE_DIR}/include/*.h")
file(GLOB LIBYARAFT_INSTALL_PB "${PROTO_DIR}/*.h" "${PROTO_DIR}/*.proto")
install(TARGETS yaraft DESTINATION lib)
install(FILES ${LIBYARAFT_INSTALL_HEADERS} DESTINATION include/yaraft)
install(FILES ${LIBYARAFT_INSTALL_PB} DESTINATION include/yaraft/pb)

set(YARAFT_TEST_LINK_LIBS ${YARAFT_TEST_LINK_LIBS} yaraft)

function(ADD_YARAFT_TEST TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cc)
    target_link_libraries(${TEST_NAME} ${YARAFT_TEST_LINK_LIBS})
endfunction()

if(${BUILD_TEST})
    ADD_YARAFT_TEST(memory_storage_test)
    ADD_YARAFT_TEST(util_test)
    ADD_YARAFT_TEST(progress_test)
    ADD_YARAFT_TEST(raft_log_test)
    ADD_YARAFT_TEST(raft_test)
    ADD_YARAFT_TEST(raft_paper_test)
    ADD_YARAFT_TEST(unstable_test)
    ADD_YARAFT_TEST(raw_node_test)
endif()