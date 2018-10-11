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

#include <memory>

#include <gtest/gtest.h>

#include "exception.h"
#include "memory_storage_impl.h"
#include "test_utils.h"

namespace yaraft {

class MemoryStorageTest : public BaseTest {
 public:
  void TestStorageTerm() {
    auto ents = {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5)};

    struct TestData {
      uint64_t i;

      error_s werr;
      uint64_t wterm;
    } tests[] = {{2, ErrCompacted, 0},
                 {3, error_s::ok(), 3},
                 {4, error_s::ok(), 4},
                 {5, error_s::ok(), 5},
                 {6, ErrUnavailable, 0}};

    for (auto tt : tests) {
      MemStoreUptr s(new MemoryStorage());
      s->ents_ = ents;

      auto errTerm = s->Term(tt.i);
      ASSERT_EQ(errTerm.get_error(), tt.werr);

      if (tt.werr.is_ok()) {
        uint64_t term = errTerm.get_value();
        ASSERT_EQ(term, tt.wterm);
      }
    }
  }

  void TestStorageEntries() {
    auto maxUInt64 = std::numeric_limits<uint64_t>::max();
    auto ents = idl::EntryVec{newEntry(3, 3), newEntry(4, 4), newEntry(5, 5), newEntry(6, 6)};

    struct TestData {
      uint64_t lo, hi, maxSize;

      error_s werr;
      idl::EntryVec wentries;
    } tests[] = {
        {2, 6, maxUInt64, ErrCompacted, {}},
        {3, 4, maxUInt64, ErrCompacted, {}},
        {4, 5, maxUInt64, error_s::ok(), {newEntry(4, 4)}},
        {4, 6, maxUInt64, error_s::ok(), {newEntry(4, 4), newEntry(5, 5)}},
        {4, 7, maxUInt64, error_s::ok(), {newEntry(4, 4), newEntry(5, 5), newEntry(6, 6)}},

        // even if maxsize is zero, the first entry should be returned
        {4, 7, 0, error_s::ok(), {newEntry(4, 4)}},

        // limit to 2
        {4, 7, uint64_t(ents[0].ByteSize() + ents[1].ByteSize()), error_s::ok(), {newEntry(4, 4), newEntry(5, 5)}},
        {4, 7, uint64_t(ents[0].ByteSize() + ents[1].ByteSize() + ents[2].ByteSize() / 2), error_s::ok(), {newEntry(4, 4), newEntry(5, 5)}},
        {4, 7, uint64_t(ents[0].ByteSize() + ents[1].ByteSize() + ents[2].ByteSize() - 1), error_s::ok(), {newEntry(4, 4), newEntry(5, 5)}},

        // all
        {4, 7, uint64_t(ents[0].ByteSize() + ents[1].ByteSize() + ents[2].ByteSize()), error_s::ok(), {newEntry(4, 4), newEntry(5, 5), newEntry(6, 6)}},
    };

    for (auto &t : tests) {
      MemStoreUptr storage(new MemoryStorage());
      storage->ents_ = ents;

      auto errEnts = storage->Entries(t.lo, t.hi, t.maxSize);
      if (!errEnts.is_ok()) {
        ASSERT_EQ(errEnts.get_error(), t.werr);
      } else {
        EntryVec_ASSERT_EQ(errEnts.get_value(), t.wentries);
      }
    }
  }

  void TestStorageLastIndex() {
    auto ents = {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5)};
    MemStoreUptr s(new MemoryStorage());
    s->ents_ = ents;

    auto errLast = s->LastIndex();
    ASSERT_TRUE(errLast.is_ok());
    ASSERT_EQ(errLast.get_value(), 5);

    idl::EntryVec v{newEntry(6, 5)};
    auto err = s->Append(v);
    ASSERT_TRUE(err.is_ok());

    errLast = s->LastIndex();
    ASSERT_TRUE(errLast.is_ok());
    ASSERT_EQ(errLast.get_value(), 6);
  }

  void TestStorageFirstIndex() {
    auto ents = {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5)};
    MemStoreUptr s(new MemoryStorage());
    s->ents_ = ents;

    auto errFirst = s->FirstIndex();
    ASSERT_TRUE(errFirst.is_ok());
    ASSERT_EQ(errFirst.get_value(), 4);

    s->Compact(4);

    errFirst = s->FirstIndex();
    ASSERT_TRUE(errFirst.is_ok());
    ASSERT_EQ(errFirst.get_value(), 5);
  }

  void TestStorageCompact() {
    auto ents = {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5)};

    struct TestData {
      uint64_t i;

      error_s werr;
      uint64_t windex;
      uint64_t wterm;
      int wlen;
    } tests[] = {
        {2, ErrCompacted, 3, 3, 3},
        {3, ErrCompacted, 3, 3, 3},
        {4, error_s::ok(), 4, 4, 2},
        {5, error_s::ok(), 5, 5, 1},
    };

    for (auto t : tests) {
      MemStoreUptr storage(new MemoryStorage());
      storage->ents_ = ents;

      auto err = storage->Compact(t.i);
      ASSERT_EQ(err, t.werr);
      ASSERT_EQ(storage->ents_[0].index(), t.windex);
      ASSERT_EQ(storage->ents_[0].term(), t.wterm);
      ASSERT_EQ(storage->ents_.size(), t.wlen);
    }
  }

  void TestStorageCreateSnapshot() {
    auto ents = {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5)};
    auto cs = idl::ConfState().nodes({1, 2, 3});
    std::string data = "data";

    struct TestData {
      uint64_t i;

      error_s werr;
      idl::Snapshot wsnap;
      int wlen;
    } tests[] = {
        {4, error_s::ok(), idl::Snapshot().metadata_term(4).metadata_index(4).metadata_conf_state(cs).data(data)},
        {5, error_s::ok(), idl::Snapshot().metadata_term(5).metadata_index(5).metadata_conf_state(cs).data(data)},
    };

    MemStoreUptr s(new MemoryStorage());

    for (auto t : tests) {
      MemStoreUptr storage(new MemoryStorage());
      storage->ents_ = ents;
      auto errWithSnap = storage->CreateSnapshot(t.i, &cs, data);
      ASSERT_EQ(errWithSnap.get_error(), t.werr);
      ASSERT_EQ(errWithSnap.get_value(), t.wsnap);
    }
  }

  void TestStorageAppend() {
    auto ents = idl::EntryVec{newEntry(3, 3), newEntry(4, 4), newEntry(5, 5)};
    struct TestData {
      idl::EntryVec entries;

      error_s werr;
      idl::EntryVec went;
    } tests[] = {
        {
            {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5)},
            error_s::ok(),
            {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5)},
        },
        {
            {newEntry(3, 3), newEntry(4, 6), newEntry(5, 6)},
            error_s::ok(),
            {newEntry(3, 3), newEntry(4, 6), newEntry(5, 6)},
        },
        {
            {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5), newEntry(6, 5)},
            error_s::ok(),
            {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5), newEntry(6, 5)},
        },
        // truncate incoming entries, truncate the existing entries and append
        {
            {newEntry(2, 3), newEntry(4, 4), newEntry(5, 5), newEntry(6, 5)},
            error_s::ok(),
            {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5), newEntry(6, 5)},
        },
        // truncate the existing entries and append
        {
            {newEntry(4, 5)},
            error_s::ok(),
            {newEntry(3, 3), newEntry(4, 5)},
        },
        // direct append
        {
            {newEntry(6, 5)},
            error_s::ok(),
            {newEntry(3, 3), newEntry(4, 4), newEntry(5, 5), newEntry(6, 5)},
        },
    };

    for (auto t : tests) {
      MemStoreUptr storage(new MemoryStorage());
      storage->ents_ = ents;
      storage->Append(t.entries);
      EntryVec_ASSERT_EQ(storage->ents_, t.went);
    }
  }

  void TestStorageApplySnapshot() {
    auto cs = idl::ConfState().nodes({1, 2, 3});
    std::string data = "data";

    idl::Snapshot tests[] = {
        idl::Snapshot().metadata_term(4).metadata_index(4).metadata_conf_state(cs),
        idl::Snapshot().metadata_term(3).metadata_index(3).metadata_conf_state(cs),
    };

    MemStoreUptr s(new MemoryStorage());

    //Apply Snapshot successful
    int i = 0;
    auto tt = tests[i];
    auto err = s->ApplySnapshot(tt);
    ASSERT_TRUE(err.is_ok());

    //Apply Snapshot fails due to ErrSnapOutOfDate
    i = 1;
    tt = tests[i];
    err = s->ApplySnapshot(tt);
    ASSERT_EQ(err, ErrSnapOutOfDate);
  }
};

TEST_F(MemoryStorageTest, Term) {
  TestStorageTerm();
}

TEST_F(MemoryStorageTest, Entries) {
  TestStorageEntries();
}

TEST_F(MemoryStorageTest, LastIndex) {
  TestStorageLastIndex();
}

TEST_F(MemoryStorageTest, FirstIndex) {
  TestStorageFirstIndex();
}

TEST_F(MemoryStorageTest, Compact) {
  TestStorageCompact();
}

TEST_F(MemoryStorageTest, CreateSnapshot) {
  TestStorageCreateSnapshot();
}

TEST_F(MemoryStorageTest, Append) {
  TestStorageAppend();
}

TEST_F(MemoryStorageTest, ApplySnapshot) {
  TestStorageApplySnapshot();
}

}  // namespace yaraft
