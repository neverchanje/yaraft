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
    uint64_t to = m.to(), from = m.from();

    if (peers_.find(to) == peers_.end() || cutMap_[from] == to) {
      return;
    }

    if (m.type() != pb::MsgVote && m.type() != pb::MsgPreVote) {
      if (peers_.find(from) != peers_.end())
        m.set_term(peers_[from]->currentTerm_);
    }

    peers_[to]->Step(m);

    auto& responses = peers_[to]->mails_;
    for (auto& resp : responses) {
      mailTo_[resp.to()].emplace_back(std::move(resp));
    }
    responses.clear();
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

  void RaiseElection(uint64_t cand = 1) {
    Send(PBMessage().From(cand).To(cand).Type(pb::MsgHup).v);

    if (Peer(cand)->c_->preVote) {
      for (uint64_t id = 1; id <= PeerSize(); id++) {
        if (id == cand)
          continue;
        TakeSingleRound(pb::MsgPreVote, cand, id);
      }

      // Leave if the election doesn't progress.
      if (Peer(cand)->role_ == Raft::kPreCandidate) {
        return;
      }
    }

    for (uint64_t id = 1; id <= PeerSize(); id++) {
      if (id == cand)
        continue;
      TakeSingleRound(pb::MsgVote, cand, id);
    }

    if (peers_[cand]->role_ == Raft::kLeader) {
      ReplicateAppend(cand);
    }
  }

  void ReplicateAppend(uint64_t lead) {
    for (uint64_t id = 1; id <= PeerSize(); id++) {
      if (id == lead)
        continue;
      TakeSingleRound(pb::MsgApp, lead, id);
    }
  }

  // Simulate a single round of request response.
  void TakeSingleRound(pb::MessageType type, uint64_t from, uint64_t to) {
    // Drop the request if the remote peer is dead or the connection to remote is cut down.
    auto req = MustTake(from, to, type);
    if (cutMap_[from] != to && peers_.find(to) != peers_.end()) {
      Send(req);
      auto resp = MustTake(to, from, responseType(type));
      Send(resp);
    }
  }

  pb::Message MustTake(uint64_t from, uint64_t to, pb::MessageType type) {
    auto m = Take(from, to);
    auto err = fmt::format(" Message [from: {}, to: {}, type: {}] not exists ", from, to,
                           pb::MessageType_Name(type));
    DLOG_ASSERT(bool(m)) << err;
    DLOG_ASSERT(m->type() == type) << err;
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

  Config* MutablePeerConfig(uint64_t id) {
    return const_cast<Config*>(peers_[id]->c_.get());
  }

  uint64_t PeerSize() const {
    return size_;
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
  pb::MessageType responseType(pb::MessageType type) {
    switch (type) {
      case pb::MsgApp:
        return pb::MsgAppResp;
      case pb::MsgVote:
        return pb::MsgVoteResp;
      case pb::MsgPreVote:
        return pb::MsgPreVoteResp;
      default:
        DLOG(FATAL) << "Error type: " << pb::MessageType_Name(type);
        return pb::MessageType(0);
    }
  }

 private:
  std::unordered_map<uint64_t, Raft*> peers_;
  size_t size_;

  // to => Message
  std::unordered_map<uint64_t, std::list<pb::Message>> mailTo_;

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

inline std::ostream& operator<<(std::ostream& os, const EntryVec& v) {
  os << "Size of entry vec: " << v.size() << ". | ";
  for (const auto& e : v) {
    os << DumpPB(e) << " | ";
  }
  return os << "\n";
}

uint64_t mustTerm(const RaftLog& log, uint64_t index) {
  auto s = log.Term(index);
  return s.OK() ? s.GetValue() : 0;
}

static uint64_t noLimit = std::numeric_limits<uint64_t>::max();

}  // namespace yaraft
