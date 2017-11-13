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

yaraft is also the internal state-machine of [consensus-yaraft](https://github.com/neverchanje/consensus-yaraft),
a highly reliable log storage library. The latter gives a full example of how to use yaraft to replicate logs.

## Features

yaraft doesn't support the [full list of features](https://github.com/coreos/etcd/tree/master/raft#features) 
included in etcd/raft.

- [x] Leader election
- [x] Log replication
- [x] Log Compaction / InstallSnapshot
- [x] Membership changes
- [ ] Leader Transfer
- [x] Linearizable read-only queries
- [x] Pipelining
- [ ] Flow Control
- [x] Batching Raft messages
- [x] Batching log entries
- [ ] Proposal forwarding from followers to leader
- [ ] CheckQuorum
- [x] PreVote

Read [docs/features.md](docs/features.md) for more information.

## APIs

Read [docs/index.md](docs/index.md) for more details.

The public interfaces are under include/*.

yaraft provides identical APIs as etcd/raft, if you are familiar with the latter,

- **include/conf.h**: The configuration to control over the behavior of raft state machine.

- **include/raw_node.h**: Main interface for controlling over raft state machine.

- **include/memory_storage.h**: A copy of the uncompacted log entries that are kept in memory for efficient retrieval. 

- **include/ready.h**: The output of the state machine.

- **src/yaraft/pb/**: The protobuf messages sent and received by yaraft. Read [docs/message_types.md](docs/message_types.md) for more information.

- **include/logger.h**: Logger is the interface for writing log messages. You can customize the
logging mechanism by implementing the interface. By default logs are simply written to stderr.

- **include/status.h**: Status is returned from many of the public interfaces and is used to
report success and various kinds of errors.

- **include/slice.h**: Slice is a [std::string_view(c++17)](http://en.cppreference.com/w/cpp/string/basic_string_view)
equivalent.

## Building from source

```bash
sudo apt-get -y install libboost-dev
bash install_deps_if_neccessary.sh
cd build && cmake .. && make && make install
```

### Running unit tests

Before running the unit tests of yaraft, you must change the option of 
cmake and rebuild the project:

```bash
cd build
cmake .. -DBUILD_TEST=On
make -j8
```

you can use [run-tests.sh](run-tests.sh) to run all tests in once.

## RoadMap

- Thrift Support: See [this](docs/index.md) for more details. 

- Introduce [Jepsen](http://jepsen.io/) testing

- Optimizations:
  - Since the original code was written in Go, which doesn't care much about
  object copies mostly, we may need some optimization on reducing the copies.

## Contributing

If you'd like to contribute, please follow the standard github best practices:
fork, fix, commit and send pull request for review.

## Contacts

Contact me or submit an issue if you have any questions about the project.

- **Mail**: wutao1@xiaomi.com

## License

yaraft is under the Apache 2.0 license. See the LICENSE file for details.
