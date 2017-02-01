
if(BUILD_TEST)
    add_executable(memory_storage_test
        memory_storage_test.cc)
    target_link_libraries(memory_storage_test gtest gtest_main)
endif()
