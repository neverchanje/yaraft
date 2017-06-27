// Copyright 2017 Wu Tao
// Copyright 2015 The etcd Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "storage.h"

#include <yaraft/pb/raftpb.pb.h>

namespace yaraft {

// Ready encapsulates the entries and messages that are ready to read,
// be saved to stable storage, committed or sent to other peers.
// All fields in Ready are read-only.
struct Ready {
  // The current state of a Node to be saved to stable storage BEFORE
  // Messages are sent.
  // hardState will be equal to empty state if there is no update.
  std::unique_ptr<pb::HardState> hardState;

  // `entries` specifies entries to be saved to stable storage BEFORE
  // Messages are sent.
  const EntryVec* entries;

  // Snapshot specifies the snapshot to be saved to stable storage.
  std::unique_ptr<pb::Snapshot> snapshot;

  // Messages specifies outbound messages to be sent AFTER Entries are
  // committed to stable storage.
  // If it contains a MsgSnap message, the application MUST report back to raft
  // when the snapshot has been received or has failed by calling ReportSnapshot.
  std::vector<pb::Message> messages;
};

inline bool IsReadyEmpty(const Ready& rd) {
  return (!rd.hardState) && rd.entries->empty() && (!rd.snapshot) && rd.messages.empty();
}

}  // namespace yaraft