// Copyright 2017 Wu Tao
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

#include "raftpb.pb.h"

namespace yaraft {

// This file provides utilities to construct protobuf structs in fluent style.

struct PBMessage {
  pb::Message v;

  PBMessage& Term(uint64_t term) {
    v.set_term(term);
    return *this;
  }

  PBMessage& LogTerm(uint64_t logTerm) {
    v.set_logterm(logTerm);
    return *this;
  }

  PBMessage& Type(pb::MessageType type) {
    v.set_type(type);
    return *this;
  }

  PBMessage& To(uint64_t to) {
    v.set_to(to);
    return *this;
  }

  PBMessage& From(uint64_t from) {
    v.set_from(from);
    return *this;
  }

  PBMessage& Index(uint64_t index) {
    v.set_index(index);
    return *this;
  }

  PBMessage& Commit(uint64_t commit) {
    v.set_commit(commit);
    return *this;
  }
};

struct PBEntry {
  pb::Entry v;

  PBEntry& Type(pb::EntryType type) {
    v.set_type(type);
    return *this;
  }

  PBEntry& Index(uint64_t index) {
    v.set_index(index);
    return *this;
  }

  PBEntry& Term(uint64_t term) {
    v.set_term(term);
    return *this;
  }
};

struct PBSnapshot {
  pb::Snapshot v;

  PBSnapshot& Metadata(pb::SnapshotMetadata data) {
    v.set_allocated_metadata(&data);
    return *this;
  }
};

struct PBSnapshotMetadata {
  pb::SnapshotMetadata v;

  PBSnapshotMetadata& Index(uint64_t index) {
    v.set_index(index);
    return *this;
  }

  PBSnapshotMetadata& Term(uint64_t term) {
    v.set_term(term);
    return *this;
  }
};

}  // namespace yaraft
