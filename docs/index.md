    Copyright 2017 Wu Tao
    Copyright 2015 The etcd Authors
    
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at
    
        http://www.apache.org/licenses/LICENSE-2.0
    
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

yaraft
======

yaraft provides identical APIs as etcd/raft, if you are familiar with the latter, feel free to skip the usage sections.

## Initializing a raft state machine

In yaraft, a raft state machine object is a RawNode.

To start a node from scratch:
```cpp
  auto storage = new yaraft::MemoryStorage;
  auto conf = new yaraft::Config;
  conf->electionTick = 10;
  conf->heartbeatTick = 1;
  conf->id = 0x01;
  conf->storage = storage;
  // Set peer list to the other nodes in the cluster.
  // Note that they need to be started separately as well.
  conf->peers = {0x01, 0x02, 0x03};
  auto raft = new RawNode(conf);
```

To start a node from previous state:

```cpp
  auto storage = new yaraft::MemoryStorage;
  storage->SetHardState(hardstate);
  storage->Append(entries);
  storage->ApplySnapshot(snap);

  auto conf = new yaraft::Config;
  conf->electionTick = 10;
  conf->heartbeatTick = 1;
  conf->id = 0x01;
  conf->storage = storage;
  // Set peer list to the other nodes in the cluster.
  // Note that they need to be started separately as well.
  conf->peers = {0x01, 0x02, 0x03};

  auto raft = new RawNode(conf);
```

## Performing command on the state machine

After creating a Node, the user has a few responsibilities:

First, read from the `RawNode::GetReady()` and process the updates it contains.
These steps must be performed in sequence:

1. Write HardState, Entries, and Snapshot to persistent storage if they are not empty. Note that when writing an Entry with Index i, any previously-persisted entries with `Index >= i` must be discarded.

2. Send all Messages to the nodes named in the `To` field. It is important that no messages be sent until the latest HardState has been persisted to disk, and all Entries written by any previous Ready batch (Messages may be sent while entries from the same batch are being persisted). To reduce the I/O latency, an optimization can be applied to make leader write to disk in parallel with its followers (as explained at section 10.2.1 in Raft thesis). If any Message has type MsgSnap, call `RawNode::ReportSnapshot()` after it has been sent (these messages may be large). Note: Marshalling messages is not thread-safe; it is important to make sure that no new entries are persisted while marshalling. The easiest way to achieve this is to serialise the messages directly inside the main raft loop.

3. Apply Snapshot (if any) and CommittedEntries to the state machine. If any committed Entry has Type `EntryConfChange`, call `RawNode::ApplyConfChange()` to apply it to the node. The configuration change may be cancelled at this point by setting the NodeID field to zero before calling ApplyConfChange (but ApplyConfChange must be called one way or the other, and the decision to cancel must be based solely on the state machine and not external information such as the observed health of the node).

4. Call `RawNode::Advance()` to signal readiness for the next batch of updates. This may be done at any time after step 1, although all updates must be processed in the order they were returned by Ready.

Second, all persisted log entries must be made available via an implementation of the Storage interface. The provided MemoryStorage type can be used for this (if repopulating its state upon a restart), or a custom disk-backed implementation can be supplied.

Third, after receiving a message from another node, pass it to `RawNode::Step`:

```cpp
void Step(pb::Message& m) {
    n.Step(m)
}
```

Finally, call `Node.Tick()` at regular intervals (probably via an async timer). Raft has two important timeouts: heartbeat and the election timeout. However, internally to yaraft, time is represented by an abstract "tick".

To propose changes to the state machine from the node to take application data, serialize it into a byte slice and call:

```cpp
  n.Propose(data)
```

If the proposal is committed, data will appear in committed entries with type raftpb.EntryNormal. There is no guarantee that a proposed command will be committed; the command may have to be reproposed after a timeout. 

To add or remove node in a cluster, build ConfChange struct 'cc' and call:

```cpp
  n.ProposeConfChange(cc)
```

After config change is committed, some committed entry with type raftpb.EntryConfChange will be returned. This must be applied to node through:

```cpp
  yaraft::pb::ConfChange cc;
  cc.ParseFromArray(data.data(), data.size());
  n.ApplyConfChange(cc)
```

**Note**: An ID represents a unique node in a cluster for all time. A
given ID MUST be used only once even if the old node has been removed.
This means that for example IP addresses make poor node IDs since they
may be reused. Node IDs must be non-zero.

## Serialization Protocol

Currently we use Protobuf as the IDL and serialization protocol.
Thrift support is in the road map.

All proto/proto-generated files are placed under **src/pb/** directory.

Callers are recommended to use **include/fluent_pb.h** to construct
proto structures, in the [fluent](https://en.wikipedia.org/wiki/Fluent_interface) style.
For example:

```cpp
  pb::Message msg = PBMessage().From(1).To(1).Type(pb::MsgHup).Term(1).v;
```

Here we create a `pb::Message` with from=1, to=1, type=pb::MsgHup, term=1, 
only in single line of code.

## Slice

Slice is a module of [silly library](https://github.com/IppClub/silly).
It's an alternative to std::string_view(C++17), boost::string_view(after boost ver1.60), boost::string_ref(deprecated),
but we ensures no compatibility issues in Slice.
The whole silly library is header-only, and requires only the C++11 compiler.

## Status

Status are returned by most functions in yaraft that may encounter an error. 
You can check if such a result is ok, and also print an associated error message:

```cpp
  yaraft::Status s = ...;
  if (!s.IsOK()) cerr << s.ToString() << endl;
```

StatusWith is designed for the functions that may return either a value or an error:

```cpp
  yaraft::StatusWith<int> s = ...;
  if (!s.IsOK()) 
      cerr << s.ToString() << endl;
  else
      cerr << s.GetValue() << endl;
```

Status is a module of silly library. It's a template class that accepts various types of error.
`yaraft::Status` is the specialized Status that reports only errors of type `yaraft::Error`.

## Logger

Logger is an generalized interface for writing log messages. By default logs are written to stderr 
using `StderrLogger`(src/stderr_logger.h/).

Customize your own logging tool and use it in yaraft through `yaraft::SetLogger` API.

```cpp
  std::unique_ptr<yaraft::Logger> logger(new StderrLogger());
  yaraft::SetLogger(std::move(logger));
```

### Use glog as the logging util

[google/glog](https://github.com/google/glog) is a widely used logging library in c++ world.
Here we give an example of how to use glog to implement the interface of `yaraft::Logger`:

```cpp
  class GLogLogger : public yaraft::Logger {
  public:
    void Log(yaraft::LogLevel level, int line, const char* file, const yaraft::Slice& log) override {
      google::LogSeverity severity;
      switch (level) {
        case yaraft::INFO:
          severity = google::INFO;
          break;
        case yaraft::WARNING:
          severity = google::WARNING;
          break;
        case yaraft::ERROR:
          severity = google::ERROR;
          break;
        case yaraft::FATAL:
          severity = google::FATAL;
          break;
        default:
          fprintf(stderr, "unsupported level of log: %d\n", static_cast<int>(level));
          assert(false);
      }
    
      google::LogMessage(file, line, severity).stream() << log;
    }
  };
```
