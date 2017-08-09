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

#include <list>

#include "conf.h"
#include "memory_storage.h"
#include "pb_utils.h"
#include "raft.h"

#include <gtest/gtest.h>

namespace yaraft {

Config* newTestConfig(uint64_t id, std::vector<uint64_t> peers, int election, int heartbeat,
                      Storage* storage) {
  auto conf = new Config();
  conf->id = id;
  conf->electionTick = election;
  conf->heartbeatTick = heartbeat;
  conf->storage = storage;
  conf->peers = std::move(peers);
  conf->maxSizePerMsg = std::numeric_limits<uint64_t>::max();
  conf->preVote = false;
  return conf;
}

Raft* newTestRaft(uint64_t id, std::vector<uint64_t> peers, int election, int heartbeat,
                  Storage* storage) {
  return new Raft(newTestConfig(id, peers, election, heartbeat, storage));
}

struct Network {
  explicit Network(std::vector<Raft*> prs) {
    for (auto r : prs) {
      peers_[r->Id()] = r;
    }
    size_ = prs.size();
  }

  ~Network() {
    for (auto& e : peers_) {
      delete e.second;
    }
  }

  void Send(pb::Message m) {
    msgs_.push_back(m);
    while (!msgs_.empty()) {
      m = msgs_.front();
      msgs_.pop_front();

      // Drop the message if the remote peer is dead or the connection to remote is cut down.
      uint64_t to = m.to(), from = m.from();
      if (peers_.find(to) == peers_.end() || cutMap_[from] == to) {
        continue;
      }

      peers_[to]->Step(m);
      for (auto& msg : peers_[to]->mails_) {
        msgs_.push_back(msg);
      }
      peers_[to]->mails_.clear();
    }
  }

  void StartElection(uint64_t cand = 1) {
    Send(PBMessage().From(cand).To(cand).Type(pb::MsgHup).v);
  }

  void Propose(uint64_t id, std::string data = "somedata") {
    Send(PBMessage().From(id).To(id).Type(pb::MsgProp).Entries({PBEntry().Data(data).v}).v);
  }

  static Network* New(uint64_t size) {
    std::vector<uint64_t> ids(size);

    uint64_t i = 1;
    std::generate(ids.begin(), ids.end(), [&]() { return i++; });

    i = 1;
    std::vector<Raft*> peers(size);

    std::generate(peers.begin(), peers.end(),
                  [&]() { return newTestRaft(i++, ids, 10, 1, new MemoryStorage()); });

    return new Network(peers);
  }

  Raft* Peer(uint64_t id) {
    DLOG_ASSERT(peers_.find(id) != peers_.end());
    return peers_[id];
  }

  std::vector<Raft*> Peers() {
    std::vector<Raft*> prs;
    for (auto& e : peers_) {
      prs.push_back(e.second);
    }
    return prs;
  }

  Config* MutablePeerConfig(uint64_t id) {
    return const_cast<Config*>(peers_[id]->c_.get());
  }

  void SetPreVote(bool preVote) {
    for (auto& p : peers_) {
      MutablePeerConfig(p.second->id_)->preVote = preVote;
    }
  }

  Network* Set(Raft* r) {
    if (peers_.find(r->Id()) != peers_.end()) {
      delete peers_[r->Id()];
    }
    peers_[r->Id()] = r;
    return this;
  }

  Network* Down(uint64_t id) {
    delete peers_[id];
    peers_.erase(peers_.find(id));
    return this;
  }

  // Cut down the connection between n1 and n2.
  void Cut(uint64_t n1, uint64_t n2) {
    cutMap_[n1] = n2;
    cutMap_[n2] = n1;
  }

  // Restore the connection between n1 and n2.
  void Restore(uint64_t n1, uint64_t n2) {
    cutMap_.erase(cutMap_.find(n1));
    cutMap_.erase(cutMap_.find(n2));
  }

 private:
  std::unordered_map<uint64_t, Raft*> peers_;
  size_t size_;

  // to => Message
  std::unordered_map<uint64_t, std::list<pb::Message>> mailTo_;
  std::list<pb::Message> msgs_;

  std::unordered_map<uint64_t, uint64_t> cutMap_;
};

pb::Entry pbEntry(uint64_t index, uint64_t term) {
  pb::Entry tmp;
  tmp.set_term(term);
  tmp.set_index(index);
  return tmp;
}

EntryVec& operator<<(EntryVec& v, const pb::Entry& e) {
  v.push_back(e);
  return v;
}

EntryVec& operator<<(EntryVec& v, const EntryVec& v2) {
  for (const auto& e : v2)
    v << e;
  return v;
}

EntryVec operator+(EntryVec v, pb::Entry e) {
  v.push_back(e);
  return v;
}

EntryVec operator+(pb::Entry e1, pb::Entry e2) {
  EntryVec v;
  v.push_back(e1);
  v.push_back(e2);
  return v;
}

static uint64_t noLimit = std::numeric_limits<uint64_t>::max();

#define Entry_ASSERT_EQ(expected, actual) \
  ASSERT_EQ((expected).DebugString(), (actual).DebugString());

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
