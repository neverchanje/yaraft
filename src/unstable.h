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

#include <memory>
#include <vector>

#include <yaraft/idl_wrapper.h>
#include <yaraft/logger.h>
#include <yaraft/span.h>

#include "make_unique.h"
#include "util.h"

namespace yaraft {

/// unstable.entries[i] has raft log position i+unstable.offset.
/// Note that unstable.offset may be less than the highest log
/// position in storage; this means that the next write to storage
/// might need to truncate the log before persisting unstable.entries.
struct Unstable {
  /// maybeFirstIndex returns the index of the first possible entry in entries
  /// if it has a snapshot.
  std::pair<uint64_t, bool> maybeFirstIndex() const {
    if (snapshot != nullptr) {
      return {snapshot->metadata_index() + 1, true};
    }
    return {0, false};
  }

  /// maybeLastIndex returns the last index if it has at least one
  /// unstable entry or snapshot.
  std::pair<uint64_t, bool> maybeLastIndex() const {
    if (!entries.empty()) {
      return {offset + entries.size() - 1, true};
    }
    if (snapshot != nullptr) {
      return {snapshot->metadata_index(), true};
    }
    return {0, false};
  }

  /// maybeTerm returns the term of the entry at index i, if there
  /// is any.
  std::pair<uint64_t, bool> maybeTerm(uint64_t i) const {
    if (i < offset) {
      if (snapshot == nullptr) {
        return {0, false};
      }
      if (snapshot->metadata_index() == i) {
        return {snapshot->metadata_term(), true};
      }
      return {0, false};
    }

    auto p = maybeLastIndex();
    uint64_t last = p.first;
    bool ok = p.second;
    if (!ok) {
      return {0, false};
    }
    if (i > last) {
      return {0, false};
    }
    return {entries[i - offset].term(), true};
  }

  void stableTo(uint64_t i, uint64_t t) {
    auto p = maybeTerm(i);
    bool ok = p.second;
    if (!ok) {
      return;
    }

    // if i < offset, term is matched with the snapshot
    // only update the unstable entries if term is matched with
    // an unstable entry.
    uint64_t gt = p.first;
    if (gt == t && i >= offset) {
      entries.SliceFrom(i + 1 - offset);  // u.entries = u.entries[i+1-u.offset:]
      offset = i + 1;
      shrinkEntriesArray();
    }
  }

  // shrinkEntriesArray discards the underlying array used by the entries slice
  // if most of it isn't being used. This avoids holding references to a bunch
  // of potentially large entries that aren't needed anymore. Simply clearing
  // the entries wouldn't be safe because clients might still be using them.
  void shrinkEntriesArray() {
    // We replace the array if we're using less than half of the space in
    // it. This number is fairly arbitrary, chosen as an attempt to balance
    // memory usage vs number of allocations. It could probably be improved
    // with some focused tuning.
    const int lenMultiple = 2;
    if (entries.empty()) {
      entries = {};
    } else if (entries.size() * lenMultiple < entries.capacity()) {
      idl::EntryVec newEntries;
      newEntries.Append(entries);
      entries = std::move(newEntries);
    }
  }

  void stableSnapTo(uint64_t i) {
    if (snapshot != nullptr && snapshot->metadata_index() == i) {
      snapshot = nullptr;
    }
  }

  void restore(idl::Snapshot s) {
    offset = s.metadata_index() + 1;
    entries = {};
    snapshot = make_unique<idl::Snapshot>();
    *(snapshot->v) = std::move(*s.v);
  }

  void truncateAndAppend(idl::EntrySpan ents) {
    uint64_t after = ents[0].index();
    if (after == offset + entries.size()) {
      // after is the next index in the u.entries
      // directly append
      entries.Append(ents);
    } else if (after <= offset) {
      RAFT_LOG(INFO, "replace the unstable entries from index %d", after);
      // The log is being truncated to before our current offset
      // portion, so set the offset and replace the entries
      offset = after;
      entries = ents;
    } else {
      // truncate to after and copy to u.entries
      // then append
      RAFT_LOG(INFO, "truncate the unstable entries before index %d", after);
      // u.entries = append([]pb.Entry{}, u.slice(u.offset, after)...)
      entries = slice(offset, after);
      // u.entries = append(u.entries, ents...)
      entries.Append(ents);
    }
  }

  idl::EntrySpan slice(uint64_t lo, uint64_t hi) const {
    mustCheckOutOfBounds(lo, hi);
    return entries.View().Slice(lo - offset, hi - offset);
  }

  // u.offset <= lo <= hi <= u.offset+len(u.offset)
  void mustCheckOutOfBounds(uint64_t lo, uint64_t hi) const {
    if (lo > hi) {
      RAFT_LOG(PANIC, "invalid unstable.slice %d > %d", lo, hi);
    }
    uint64_t upper = offset + entries.size();
    if (lo < offset || hi > upper) {
      RAFT_LOG(PANIC, "unstable.slice[%d,%d) out of bound [%d,%d]", lo, hi,
               offset, upper);
    }
  }

 public:
  /// the incoming unstable snapshot, if any.
  std::unique_ptr<idl::Snapshot> snapshot;

  /// all entries that have not yet been written to storage.
  idl::EntryVec entries;

  size_t offset{0};
};

}  // namespace yaraft
