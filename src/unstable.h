// Copyright 2017 The etcd Authors
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

#include <vector>

#include "fluent_pb.h"
#include "logging.h"

#include <boost/optional.hpp>

namespace yaraft {

// unstable.entries[i] has raft log position i+unstable.offset.
// Note that unstable.offset may be less than the highest log
// position in storage; this means that the next write to storage
// might need to truncate the log before persisting unstable.entries.
struct Unstable {
 public:
  // MaybeTerm returns the term of the entry at index i, if there
  // is any.
  // @returns 0 if there's no existing entry has index i.
  uint64_t MaybeTerm(uint64_t i) const {
    if (i < offset) {
      if (snapshot && snapshot->metadata().index() == i) {
        return snapshot->metadata().term();
      }
      return 0;
    }

    // i >= offset
    if (!entries.empty()) {
      if (i >= offset && i < offset + entries.size()) {
        return entries[i - offset].term();
      }
    }
    return 0;
  }

  // Required: begin != end
  // Required: after <= offset + entries.size, in other words, there's no hole between two entries.
  void TruncateAndAppend(EntriesIterator begin, EntriesIterator end) {
    uint64_t after = begin->index();
    if (after == offset + entries.size()) {
      // after is the next index in the u.entries directly append
      entries.reserve(entries.size() + std::distance(begin, end));
      std::for_each(begin, end, [&](pb::Entry& e) { entries.push_back(std::move(e)); });
    } else if (after <= offset) {
      FMT_SLOG(INFO, "replace the unstable entries from index %d", after);
      // The log is being truncated to before our current offset
      // portion, so set the offset and replace the entries
      entries.resize(std::distance(begin, end));
      for (int i = 0; i < entries.size(); i++) {
        entries[i].Swap(&(*begin++));
      }
      offset = after;
    } else {
      // offset < after < offset + entries.size
      entries.resize(after - offset + std::distance(begin, end));
      for (int i = after - offset; i < entries.size(); i++) {
        entries[i].Swap(&(*begin++));
      }
    }
  }

  // REQUIRED: all existing log entries are conflicted with the snapshot.
  void Restore(pb::Snapshot& snap) {
    offset = snap.metadata().index() + 1;
    entries.clear();
    snapshot.reset(new pb::Snapshot);
    snapshot->Swap(&snap);
  }

 public:
  size_t offset;

  // all entries that have not yet been written to storage.
  std::vector<pb::Entry> entries;

  // the incoming unstable snapshot, if any.
  std::unique_ptr<pb::Snapshot> snapshot;
};

}  // namespace yaraft
