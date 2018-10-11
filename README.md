# yaraft
[![Build Status](https://travis-ci.org/neverchanje/yaraft.svg)](https://travis-ci.org/neverchanje/yaraft)

## What is yaraft? 

yaraft is a C++11 port of [etcd/raft](https://github.com/coreos/etcd/tree/master/raft), which is a widely
proven [Raft](https://raft.github.io) library written in Go. Raft is a well-known distributed consensus 
algorithm. It's a practical solution designed for understandability, without sacrificing performance and
fault-tolerance comparing to Paxos.

etcd/raft features at its minimalistic design that only the core Raft protocol is implemented. 
No RPC, no WAL storage, no multi-threaded environment. It has nothing more than a pure state machine,
so that we can test it in a deterministic way. For state machines with the same state, the same state machine 
input should always generate the same state machine output.

## APIs

Read [docs/index.md](docs/index.md) for more details.

The public interfaces are under include/*.

- **include/conf.h**: The configuration to control over the behavior of raft state machine.

- **include/raw_node.h**: Main interface for controlling over raft state machine.

- **include/memory_storage.h**: A copy of the uncompacted log entries that are kept in memory for efficient retrieval. 

- **include/ready.h**: The output of the state machine.

- **src/yaraft/thrift/**: The thrift messages sent and received by yaraft. Read [docs/message_types.md](docs/message_types.md) for more information.

- **include/logger.h**: Logger is the interface for writing log messages. You can customize the
logging mechanism by implementing the interface. By default logs are simply written to stderr.

- **include/errors.h**: `error_s` is returned from many of the public interfaces and is used to
report success and various kinds of errors.

## Building from source

```bash
sudo apt-get -y install libboost-dev
bash install_deps_if_neccessary.sh
cd build && cmake .. && make && make install
```

### Running the tests

```bash
$ BUILD=Debug STANDARD=11 ENABLE_GCOV=false ./run_tests.sh
```

you can run [run_gcov.sh](run_gcov.sh) to generate the coverage report.

## Contacts

Contact me or submit an issue if you have any questions about the project.

- **Mail**: wutao1@xiaomi.com

## License

yaraft is under the Apache 2.0 license. See the LICENSE file for details.
