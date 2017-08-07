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
#include "logging.h"
#include "storage.h"
#include "unstable.h"

#include <silly/disallow_copying.h>

namespace yaraft {

class RaftLog {
  __DISALLOW_COPYING__(RaftLog);

 public:
  explicit RaftLog(Storage* storage) : storage_(storage), lastApplied_(0) {
    auto s = storage_->FirstIndex();
    FATAL_NOT_OK(s, "Storage::FirstIndex");

    uint64_t firstIndex = s.GetValue();
    commitIndex_ = firstIndex - 1;
    lastApplied_ = firstIndex - 1;

    s = storage_->LastIndex();
    FATAL_NOT_OK(s, "Storage::LastIndex");

    uint64_t lastIndex = s.GetValue();
    unstable_.offset = lastIndex + 1;
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

    uint64_t term = unstable_.MaybeTerm(index);
    if (term) {
      return term;
    }

    auto s = storage_->Term(index);
    if (s.IsOK()) {
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
    if (!unstable_.entries.empty()) {
      return unstable_.entries.rbegin()->index();
    } else if (unstable_.snapshot) {
      return unstable_.snapshot->metadata().index();
    } else {
      auto s = storage_->LastIndex();
      if (!s.IsOK()) {
        LOG(FATAL) << s.GetStatus();
      }
      return s.GetValue();
    }
  }

  uint64_t FirstIndex() const {
    if (unstable_.snapshot) {
      // unstable snapshot always precedes all the entries in RaftLog.
      return unstable_.snapshot->metadata().index() + 1;
    }
    auto sw = storage_->FirstIndex();
    FATAL_NOT_OK(sw, "Storage::FirstIndex");
    return sw.GetValue();
  }

  uint64_t LastTerm() const {
    auto s = Term(LastIndex());
    if (!s.IsOK()) {
      LOG(FATAL) << s.GetStatus();
    }
    return s.GetValue();
  }

  bool HasEntry(uint64_t index, uint64_t term) {
    auto s = Term(index);
    if (s.IsOK()) {
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
#ifdef BUILD_TESTS
      throw RaftError("Append a committed entry at {:d}, committed: {:d}", begin->index(),
                      commitIndex_);
#else
      FMT_LOG(FATAL, "Append a committed entry at {:d}, committed: {:d}", begin->index(),
              commitIndex_);
#endif
    }
    unstable_.TruncateAndAppend(begin, end);
  }

  void CommitTo(uint64_t to) {
    if (to > commitIndex_) {
      if (LastIndex() < to) {
        FMT_LOG(FATAL,
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
    if (lo > hi) {
      return EntryVec();
    }

    uint64_t fi = FirstIndex(), li = LastIndex();
    if (lo < fi)
      return Status::Make(Error::LogCompacted);
    LOG_ASSERT(hi <= li + 1) << fmt::format(" slice[{:d},{:d}) out of bound [{:d},{:d}]", lo, hi,
                                            fi, li);

    uint64_t uOffset = unstable_.offset;

    EntryVec ret;
    if (lo < uOffset) {
      auto s = storage_->Entries(lo, std::min(hi, uOffset), &maxSize);
      if (!s.IsOK()) {
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

  uint64_t LastApplied() const {
    return lastApplied_;
  }

  void ApplyTo(uint64_t i) {
    LOG_ASSERT(i != 0);
    if (commitIndex_ < i || i < lastApplied_) {
      FMT_SLOG(FATAL, "applied(%d) is out of range [prevApplied(%d), committed(%d)]", i,
               lastApplied_, commitIndex_);
    }
    lastApplied_ = i;
  }

  uint64_t ZeroTermOnErrCompacted(uint64_t index) const {
    auto st = Term(index);
    if (!st.IsOK()) {
      if (st.GetStatus().Code() == Error::LogCompacted) {
        return 0;
      }
      FMT_LOG(FATAL, "fail to get term for index: {}, error: {}", index, st.ToString());
    }
    return st.GetValue();
  }

  // REQUIRED: snap.metadata().index > CommittedIndex
  // REQUIRED: there's no existing log entry the same as {index: snap.metadata.index, term:
  // snap.metadata.term}.
  void Restore(pb::Snapshot& snap) {
    FMT_SLOG(INFO, "log [%s] starts to restore snapshot [index: %d, term: %d]", ToString(),
             snap.metadata().index(), snap.metadata().term());
    commitIndex_ = snap.metadata().index();
    unstable_.Restore(snap);
  }

  // TODO: use shared_ptr to reduce copying
  StatusWith<pb::Snapshot> Snapshot() const {
    if (unstable_.snapshot) {
      return *unstable_.snapshot;
    }
    return storage_->Snapshot();
  }

  std::string ToString() const {
    return fmt::sprintf("committed=%d, applied=%d, unstable.offset=%d, len(unstable.Entries)=%d",
                        commitIndex_, lastApplied_, unstable_.offset, unstable_.entries.size());
  }

 public:
  /// Used with caution

  EntryVec AllEntries() {
    auto s = Entries(FirstIndex(), LastIndex() + 1, std::numeric_limits<uint64_t>::max());
    if (!s.IsOK()) {
      DLOG(FATAL) << s.GetStatus();
    }
    return s.GetValue();
  }

  Unstable& GetUnstable() {
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
