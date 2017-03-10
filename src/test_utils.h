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
#include "raft.h"
#include "raftpb.pb.h"
#include "storage.h"

namespace yaraft {

Config* newTestConfig(uint64_t id, std::vector<uint64_t> peers, int election, int heartbeat,
                      Storage* storage) {
  auto conf = new Config();
  conf->id = id;
  conf->electionTick = election;
  conf->heartbeatTick = heartbeat;
  conf->storage = storage;
  conf->peers = std::move(peers);
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
  }

  void Send(pb::Message& m) {
    uint64_t to = m.to();
    peers_[to]->Step(m);

    auto& responses = peers_[to]->mails_;
    for (auto& resp : responses) {
      mailTo_[resp.to()].emplace_back(std::move(resp));
    }
    DLOG_ASSERT(!responses.empty());
  }

  // take responses from mailbox
  boost::optional<pb::Message> Take(uint64_t from, uint64_t to) {
    if (mailTo_.empty())
      return boost::none;

    if (mailTo_.find(to) != mailTo_.end()) {
      auto& msgs = mailTo_[to];
      auto it = std::find_if(msgs.begin(), msgs.end(),
                             [&](const pb::Message& m) { return m.from() == from; });
      if (it != msgs.end()) {
        pb::Message m = *it;
        msgs.erase(it);
        return boost::make_optional(m);
      }
    }
    return boost::none;
  }

  pb::Message MustTake(uint64_t from, uint64_t to, pb::MessageType type) {
    auto m = Take(from, to);
    DLOG_ASSERT(bool(m));
    DLOG_ASSERT(m->type() == type);
    return *m;
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

  Network* Set(Config* conf) {
    if (peers_.find(conf->id) != peers_.end()) {
      delete peers_[conf->id];
    }
    peers_[conf->id] = new Raft(conf);
    return this;
  }

 private:
  std::unordered_map<uint64_t, Raft*> peers_;

  // to => Message
  std::unordered_map<uint64_t, std::list<pb::Message>> mailTo_;
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

inline bool operator==(pb::Entry e1, pb::Entry e2) {
  bool result = (e1.term() == e2.term()) && (e1.index() == e2.index());
  return result;
}

inline bool operator!=(pb::Entry e1, pb::Entry e2) {
  return !(e1 == e2);
}

inline bool operator==(EntryVec v1, EntryVec v2) {
  if (v1.size() != v2.size())
    return false;
  auto it1 = v1.begin();
  auto it2 = v2.begin();
  while (it1 != v1.end()) {
    if (*it1++ != *it2++)
      return false;
  }
  return true;
}

inline std::ostream& operator<<(std::ostream& os, const pb::Entry& e) {
  return os << "{Index: " << e.index() << ", Term: " << e.term() << "}";
}

inline std::ostream& operator<<(std::ostream& os, const EntryVec& v) {
  os << "Size of entry vec: " << v.size() << ". | ";
  for (const auto& e : v) {
    os << e << " | ";
  }
  return os << "\n";
}

uint64_t mustTerm(const RaftLog& log, uint64_t index) {
  auto s = log.Term(index);
  return s.OK() ? s.GetValue() : 0;
}

static uint64_t noLimit = std::numeric_limits<uint64_t>::max();

}  // namespace yaraft
