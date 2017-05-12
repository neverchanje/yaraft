set(YARAFT_TEST_LINK_LIBS
        gtest gtest_main
        ${PROTOBUF_STATIC_LIBRARY}
        ${FMT_LIBRARY}
        ${GLOG_STATIC_LIB})

file(GLOB LIBYARAFT_HEADERS "${PROJECT_SOURCE_DIR}/include/*.h" "${PROJECT_SOURCE_DIR}/pb/*.h")
file(GLOB LIBYARAFT_CPP "${PROJECT_SOURCE_DIR}/src/*.cc" "${PROJECT_SOURCE_DIR}/pb/*.cc")
add_library(yaraft ${LIBYARAFT_CPP})
target_link_libraries(yaraft ${YARAFT_TEST_LINK_LIBS})

install(TARGETS yaraft DESTINATION lib)
install(FILES ${LIBYARAFT_HEADERS} DESTINATION include/yaraft)

set(YARAFT_TEST_LINK_LIBS ${YARAFT_TEST_LINK_LIBS} yaraft)

function(ADD_YARAFT_TEST TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_NAME}.cc)
    target_link_libraries(${TEST_NAME} ${YARAFT_TEST_LINK_LIBS})
endfunction()

ADD_YARAFT_TEST(memory_storage_test)

ADD_YARAFT_TEST(util_test)

ADD_YARAFT_TEST(progress_test)

ADD_YARAFT_TEST(raft_log_test)

ADD_YARAFT_TEST(raft_test)

ADD_YARAFT_TEST(raft_paper_test)

ADD_YARAFT_TEST(unstable_test)

ADD_YARAFT_TEST(raw_node_test)
