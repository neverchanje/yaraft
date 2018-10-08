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

#include <gtest/gtest.h>
#include <yaraft/conf.h>
#include <yaraft/memory_storage.h>
#include <list>
#include <unordered_map>
#include <utility>

#include "make_unique.h"
#include "raft.h"
#include "rand.h"
#include "stderr_logger.h"

namespace yaraft {

class BaseTest : public testing::Test {
 public:
  BaseTest() {
    raftLogger = make_unique<StderrLogger>();
    setRandSeed(testing::FLAGS_gtest_random_seed);
    RAFT_LOG(INFO, "use random seed: %d", testing::FLAGS_gtest_random_seed);
  }

  static Config* newTestConfig(uint64_t id, std::vector<uint64_t> peers, int election,
                               int heartbeat, Storage* storage) {
    auto conf = new Config();
    conf->id = id;
    conf->electionTick = election;
    conf->heartbeatTick = heartbeat;
    conf->storage = storage;
    conf->peers = std::move(peers);
    conf->maxSizePerMsg = std::numeric_limits<uint64_t>::max();
    conf->preVote = false;
    conf->maxInflightMsgs = 256;
    return conf;
  }

  static Raft*
  newTestLearnerRaft(uint64_t id, std::vector<uint64_t> peers, std::vector<uint64_t> learners, int election,
                     int heartbeat, Storage* storage) {
    auto cfg = newTestConfig(id, std::move(peers), election, heartbeat, storage);
    cfg->learners = std::move(learners);
    return Raft::New(cfg);
  }

  static Raft*
  newTestRaft(uint64_t id, std::vector<uint64_t> peers, int election,
              int heartbeat, Storage* storage) {
    return Raft::New(newTestConfig(id, std::move(peers), election, heartbeat, storage));
  }

  // setRandomizedElectionTimeout set up the value by caller instead of choosing
  // by system, in some test scenario we need to fill in some expected value to
  // ensure the certainty
  static void setRandomizedElectionTimeout(Raft* r, int v) {
    r->randomizedElectionTimeout_ = v;
  }

  // nextEnts returns the appliable entries and updates the applied index
  static idl::EntryVec nextEnts(Raft* r, MemoryStorage* s) {
    // Transfer all unstable entries to "stable" storage.
    s->Append(r->raftLog_->unstableEntries());
    r->raftLog_->stableTo(r->raftLog_->lastIndex(), r->raftLog_->lastTerm());

    auto ents = r->raftLog_->nextEnts();
    r->raftLog_->appliedTo(r->raftLog_->committed_);
    return ents;
  }
};

inline std::vector<uint64_t> idsBySize(size_t size) {
  std::vector<uint64_t> ids(size);
  for (int i = 0; i < size; i++) {
    ids[i] = 1 + uint64_t(i);
  }
  return ids;
}

inline Config* newTestConfig(uint64_t id, std::vector<uint64_t> peers, int election,
                             int heartbeat, Storage* storage) {
  return BaseTest::newTestConfig(id, std::move(peers), election, heartbeat, storage);
}

class BlackHole : public StateMachine {
  error_s Step(idl::Message m) override { return error_s::ok(); };
  std::vector<idl::Message> readMessages() override { return {}; };
  std::string type() const override { return "blackhole"; }
};

extern BlackHole* nopStepper;

class Network {
 public:
  struct Connem {
    uint64_t from, to;
    friend bool inline operator<(const Connem& a, const Connem& b) {
      return a.from < b.from || (a.from == b.from && a.to < b.to);
    }
  };

  std::unordered_map<uint64_t, StateMachine*> peers_;
  std::unordered_map<uint64_t, MemoryStorage*> storage_;
  std::map<Connem, double> dropm_;
  std::set<idl::MessageType> ignorem_;

  // msgHook is called for each message sent. It may inspect the
  // message and return true to send it or false to drop it.
  std::function<bool(idl::Message)> msgHook_;

  // newNetwork initializes a network from peers.
  // A nil node will be replaced with a new *stateMachine.
  // A *stateMachine will get its k, id.
  // When using stateMachine, the address list is always [1, n].
  //
  // func newNetwork(peers ...stateMachine) *network {
  explicit Network(std::vector<StateMachine*> prs) : Network(nullptr, std::move(prs)) {
  }

  ~Network() {
    for (auto kv : peers_) {
      // do not delete noStepper
      if (kv.second->type() == "raft") {
        delete kv.second;
      }
    }
  }

  // newNetworkWithConfig is like newNetwork but calls the given func to
  // modify the configuration of any state machines it creates.
  Network(std::function<void(Config*)> configFunc, std::vector<StateMachine*> peers) {
    size_t size = peers.size();
    auto peerAddrs = idsBySize(size);

    std::unordered_map<uint64_t, StateMachine*> npeers;
    std::unordered_map<uint64_t, MemoryStorage*> nstorage;

    for (int j = 0; j < peers.size(); j++) {
      uint64_t id = peerAddrs[j];
      auto p = peers[j];

      if (p == nullptr) {
        nstorage[id] = new MemoryStorage;
        auto cfg = newTestConfig(id, peerAddrs, 10, 1, nstorage[id]);
        if (configFunc != nullptr) {
          configFunc(cfg);
        }
        auto sm = Raft::New(cfg);
        npeers[id] = sm;
      } else if (p->type() == "raft") {
        auto v = static_cast<Raft*>(p);
        std::set<uint64_t> learners;
        for (const auto& kv : v->learnerPrs_) {
          uint64_t i = kv.first;
          learners.insert(i);
        }
        v->id_ = id;
        for (size_t i = 0; i < size; i++) {
          if (learners.find(peerAddrs[i]) != learners.end()) {
            v->learnerPrs_[peerAddrs[i]] = Progress().IsLearner(true).Sptr();
          } else {
            v->prs_[peerAddrs[i]] = Progress().Sptr();
          }
        }
        v->reset(v->term_);
        npeers[id] = v;
      } else if (p->type() == "blackhole") {
        auto v = static_cast<BlackHole*>(p);
        npeers[id] = v;
      } else {
        RAFT_LOG(PANIC, "unexpected state machine type: %s", p->type());
      }
    }

    peers_ = std::move(npeers);
    storage_ = std::move(nstorage);
  }

  void Send(GoSlice<idl::Message> msgs) {
    while (!msgs.empty()) {
      auto& m = msgs[0];
      auto p = peers_[m.to()];
      p->Step(m);

      // msgs = append(msgs[1:], nw.filter(p.readMessages())...)
      msgs.SliceFrom(1);
      msgs.Append(Filter(p->readMessages()));
    }
  }

  void Drop(uint64_t from, uint64_t to, double perc) {
    dropm_[Connem{from, to}] = perc;
  }

  void Cut(uint64_t one, uint64_t other) {
    Drop(one, other, 2.0);  // always drop
    Drop(other, one, 2.0);  // always drop
  }

  void Isolate(uint64_t id) {
    for (int i = 0; i < peers_.size(); i++) {
      uint64_t nid = uint64_t(i) + 1;
      if (nid != id) {
        Drop(id, nid, 1.0);  // always drop
        Drop(nid, id, 1.0);  // always drop
      }
    }
  }

  void Ignore(idl::MessageType type) {
    ignorem_.insert(type);
  }

  void Recover() {
    dropm_.clear();
    ignorem_.clear();
  }

  std::vector<idl::Message> Filter(std::vector<idl::Message> msgs) {
    std::vector<idl::Message> mm;
    for (auto m : msgs) {
      if (ignorem_.find(m.type()) != ignorem_.end()) {
        continue;
      }
      switch (m.type()) {
        case idl::MsgHup:
          // hups never go over the network, so don't drop them but panic
          RAFT_PANIC("unexpected msgHup");
        default:
          double perc = dropm_[Connem{m.from(), m.to()}];
          double n = randFloat64();
          if (n < perc) {
            continue;
          }
      }
      if (msgHook_ != nullptr) {
        if (!msgHook_(m)) {
          continue;
        }
      }
      mm.push_back(std::move(m));
    }
    return mm;
  }

  std::shared_ptr<Network> SPtr() {
    auto ret = std::make_shared<Network>(*this);
    peers_.clear();
    return ret;
  }
};

using NetworkSPtr = std::shared_ptr<Network>;

inline idl::Entry newEntry(uint64_t index, uint64_t term) {
  idl::Entry tmp;
  tmp.term(term);
  tmp.index(index);
  return tmp;
}

#define Entry_ASSERT_EQ(expected, actual) \
  ASSERT_EQ(fmt::format("{}", (expected)), fmt::format("{}", (actual)));

#define EntryVec_ASSERT_EQ(expected, actual)     \
  do {                                           \
    auto _expected = (expected);                 \
    auto _actual = (actual);                     \
    ASSERT_EQ(_expected.size(), _actual.size()); \
    for (int i = 0; i < _expected.size(); i++) { \
      Entry_ASSERT_EQ(_expected[i], _actual[i]); \
    }                                            \
  } while (0)

}  // namespace yaraft
