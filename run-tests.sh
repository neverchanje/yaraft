#!/usr/bin/env bash

set -e

TEST_DIR=`dirname $BASH_SOURCE`/build/src

function run()
{
    echo "===========" $1 "==========="
    ./$TEST_DIR/$1
    if [ $? -ne 0 ]; then
        echo "TEST FAILED!!!"
        exit 1
    fi
}

run memory_storage_test
run raft_log_test
run raft_paper_test
run raft_test
run unstable_test
run pb_utils_test
run progress_test
run raw_node_test
run raft_snap_test