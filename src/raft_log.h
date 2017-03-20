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

#include "exception.h"
#include "storage.h"
#include "unstable.h"

#include <fmt/format.h>
#include <glog/logging.h>
#include <silly/disallow_copying.h>

namespace yaraft {

class RaftLog {
  __DISALLOW_COPYING__(RaftLog);

  /// |<- firstIndex          lastIndex->|
  /// |-- Storage---|----- Unstable -----|
  /// |=============|====================|

 public:
  explicit RaftLog(Storage* storage) : storage_(storage), lastApplied_(0) {
    auto s = storage_->FirstIndex();
    if (!s.GetStatus().OK()) {
      LOG(FATAL) << s.GetStatus();
    }
    auto firstIndex = s.GetValue();
    commitIndex_ = firstIndex - 1;
  }

  // Raft determines which of two logs is more up-to-date
  // by comparing the index and term of the last entries in the
  // logs. If the logs have last entries with different terms, then
  // the log with the later term is more up-to-date. If the logs
  // end with the same term, then whichever log is longer is
  // more up-to-date. (Raft paper 5.4.1)
  bool IsUpToDate(uint64_t index, uint64_t term) {
    return (term > LastTerm()) || (term == LastTerm() && index >= LastIndex());
  }

  uint64_t CommitIndex() const {
    return commitIndex_;
  }

  StatusWith<uint64_t> Term(uint64_t index) const {
    // the valid index range is [index of dummy entry, last index]
    auto dummyIndex = FirstIndex() - 1;
    if (index > LastIndex() || index < dummyIndex) {
      return Status::Make(Error::OutOfBound);
    }

    auto pTerm = unstable_.MaybeTerm(index);
    if (pTerm) {
      return pTerm.get();
    }

    auto s = storage_->Term(index);
    if (s.OK()) {
      return s.GetValue();
    }

    auto errorCode = s.GetStatus().Code();
    if (errorCode == Error::OutOfBound || errorCode == Error::LogCompacted) {
      return s.GetStatus();
    }

    // unacceptable error
    LOG(FATAL) << s.GetStatus();
    return 0;
  }

  uint64_t LastIndex() const {
    if (!unstable_.Empty()) {
      return unstable_.entries.rbegin()->index();
    } else {
      auto s = storage_->LastIndex();
      if (!s.OK()) {
        LOG(FATAL) << s.GetStatus();
      }
      return s.GetValue();
    }
  }

  uint64_t LastTerm() const {
    auto s = Term(LastIndex());
    if (!s.OK()) {
      LOG(FATAL) << s.GetStatus();
    }
    return s.GetValue();
  }

  uint64_t FirstIndex() const {
    auto s = storage_->FirstIndex();
    if (!s.OK()) {
      LOG(FATAL) << s.GetStatus();
    }
    return s.GetValue();
  }

  bool HasEntry(uint64_t index, uint64_t term) {
    auto s = Term(index);
    if (s.OK()) {
      return s.GetValue() == term;
    }
    return false;
  }

  void Append(pb::Entry e) {
    auto msg = PBMessage().Entries({e}).v;
    Append(msg.mutable_entries()->begin(), msg.mutable_entries()->end());
  }

  void Append(EntryVec vec) {
    auto msg = PBMessage().Entries(vec).v;
    Append(msg.mutable_entries()->begin(), msg.mutable_entries()->end());
  }

  // Appends entries into unstable.
  // Entries between begin and end must be ensured not identical with the existing log entries.
  void Append(EntriesIterator begin, EntriesIterator end) {
    if (begin == end) {
      return;
    }

    if (begin->index() <= commitIndex_) {
      throw RaftError("Append a committed entry at {:d}, committed: {:d}", begin->index(),
                      commitIndex_);
    }
    unstable_.TruncateAndAppend(begin, end);
  }

  void CommitTo(uint64_t to) {
    if (to > commitIndex_) {
      if (LastIndex() < to) {
        throw RaftError(
            "tocommit({:d}) is out of range [LastIndex({:d})]. Was the raft log corrupted, "
            "truncated, or lost?",
            to, LastIndex());
      }
      commitIndex_ = to;
    }
  }

  // MaybeAppend returns false and set newLastIndex=0 if the entries cannot be appended. Otherwise,
  // it returns true and set newLastIndex = last index of new entries = prevLogIndex + len(entries).
  bool MaybeAppend(pb::Message& m, uint64_t* newLastIndex) {
    uint64_t prevLogIndex = m.index();
    uint64_t prevLogTerm = m.logterm();

    if (HasEntry(prevLogIndex, prevLogTerm)) {
      *newLastIndex = prevLogIndex + m.entries_size();

      if (m.entries_size() > 0) {
        // An entry in raft log that doesn't exist in MsgApp is defined as conflicted.
        // MaybeAppend deletes the conflicted entry and all that follows it from raft log,
        // and append new entries from MsgApp.
        auto begin = m.mutable_entries()->begin();
        auto end = m.mutable_entries()->end();

        // prevLog must be immediately before the new log.
        LOG_ASSERT(begin->index() == prevLogIndex + 1);

        for (auto& e : m.entries()) {
          if (!HasEntry(e.index(), e.term())) {
            break;
          }
          begin++;
        }

        Append(begin, end);
      }
      return true;
    }
    *newLastIndex = 0;
    return false;
  }

  StatusWith<EntryVec> Entries(uint64_t lo, uint64_t maxSize) {
    uint64_t lastIdx = LastIndex();
    return Entries(lo, lastIdx + 1, maxSize);
  }

  // Returns a slice of log entries from lo through hi-1, inclusive.
  // FirstIndex <= lo < hi <= LastIndex + 1
  StatusWith<EntryVec> Entries(uint64_t lo, uint64_t hi, uint64_t maxSize) {
    LOG_ASSERT(lo < hi);

    uint64_t fi = FirstIndex(), li = LastIndex();
    if (lo < fi)
      return Status::Make(Error::LogCompacted);
    LOG_ASSERT(hi <= li + 1) << fmt::format(" slice[{:d},{:d}) out of bound [{:d},{:d}]", lo, hi,
                                            fi, li);

    uint64_t uOffset = UnstableOffset();

    EntryVec ret;
    if (lo < uOffset) {
      auto s = storage_->Entries(lo, std::min(hi, uOffset), &maxSize);
      if (!s.OK()) {
        return s.GetStatus();
      }
      ret = std::move(s.GetValue());
    }

    if (hi > uOffset) {
      auto begin = std::max(lo, uOffset) - uOffset + unstable_.entries.begin();
      auto end = unstable_.entries.end();
      uint64_t size = 0;

      int64_t remains = hi - lo - ret.size();
      auto it = begin;
      for (; it != end && remains > 0; it++) {
        size += it->ByteSize();
        if (size > maxSize)
          break;
        remains--;
      }
      end = it;

      std::copy(begin, end, std::back_inserter(ret));
    }

    return ret;
  }

  // Returns the left bound index of entries in unstable_.
  uint64_t UnstableOffset() const {
    auto s = storage_->LastIndex();
    LOG_ASSERT(s.GetStatus().OK());
    return unstable_.Empty() ? s.GetValue() + 1 : unstable_.FirstIndex();
  }

  uint64_t LastApplied() const {
    return lastApplied_;
  }

  uint64_t ZeroTermOnErrCompacted(uint64_t index) {
    auto st = Term(index);
    if (!st.OK()) {
      if (st.GetStatus().Code() == Error::LogCompacted) {
        return 0;
      }
      LOG(FATAL) << st.GetStatus();
    }
    return st.GetValue();
  }

 public:
  /// Only used for test

  EntryVec AllEntries() {
    auto s = Entries(FirstIndex(), LastIndex() + 1, std::numeric_limits<uint64_t>::max());
    if (!s.OK()) {
      DLOG(FATAL) << s.GetStatus();
    }
    return s.GetValue();
  }

  Unstable& TEST_Unstable() {
    return unstable_;
  }

 private:
  // storage contains all stable entries since the last snapshot.
  std::unique_ptr<Storage> storage_;

  // unstable contains all unstable entries and snapshot.
  // they will be saved into storage.
  Unstable unstable_;

  /// The following variables are volatile states kept on all servers, as referenced in raft paper
  /// Figure 2.
  /// Invariant: commitIndex >= lastApplied.

  // Index of highest log entry known to be committed (initialized to 0, increases monotonically)
  uint64_t commitIndex_;
  // Index of highest log entry applied to state machine (initialized to 0, increases monotonically)
  uint64_t lastApplied_;
};

}  // namespace yaraft
