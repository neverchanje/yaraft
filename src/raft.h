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

#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "raft_log.h"
#include "rand.h"

#include <yaraft/conf.h>
#include <yaraft/logger.h>
#include <yaraft/progress.h>
#include <yaraft/raw_node.h>
#include <yaraft/ready.h>
#include <yaraft/status.h>

namespace yaraft {

// CampaignType represents the type of campaigning
// the reason we use the type of string instead of uint64
// is because it's simpler to compare and fill in raft entries
typedef std::string CampaignType;
// campaignPreElection represents the first phase of a normal election when
// Config.PreVote is true.
static const CampaignType campaignPreElection = "CampaignPreElection";
// campaignElection represents a normal (time-based) election (the second phase
// of the election when Config.PreVote is true).
static const CampaignType campaignElection = "CampaignElection";
// campaignTransfer represents the type of leader transfer
static const CampaignType campaignTransfer = "CampaignTransfer";

// kNone is a placeholder node ID used when there is no leader.
constexpr uint64_t kNone = 0;

inline idl::MessageType voteRespType(idl::MessageType voteType) {
  return voteType == idl::MsgVote ? idl::MsgVoteResp : idl::MsgPreVoteResp;
}

class StateMachine {
 public:
  virtual error_s Step(idl::Message m) = 0;
  virtual std::vector<idl::Message> readMessages() = 0;
  virtual std::string type() const = 0;

  virtual ~StateMachine() = default;
};

class Raft : public StateMachine {
  friend class RaftTest;
  friend class RaftPaperTest;
  friend class RaftSnapTest;
  friend class RaftFlowControlTest;
  friend class ProgressTest;
  friend class RawNodeTest;
  friend class BaseTest;
  friend class Network;
  friend class RawNode;

  uint64_t id_{0};

  uint64_t term_{0};
  uint64_t vote_{0};

  std::vector<ReadState> readStates_;

  // the log
  std::unique_ptr<RaftLog> raftLog_;

  size_t maxInflight_{0};
  uint64_t maxMsgSize_{0};
  std::map<uint64_t, std::shared_ptr<Progress>> prs_;
  std::map<uint64_t, std::shared_ptr<Progress>> learnerPrs_;
  std::vector<uint64_t> matchBuf_;

  StateType state_{StateType::kFollower};

  // isLearner is true if the local raft node is a learner.
  bool isLearner_{false};

  std::map<uint64_t, bool> votes_;

  // msgs to be sent are temporarily stored in MailBox.
  std::vector<idl::Message> msgs_;

  // the leader id
  uint64_t lead_{0};
  // leadTransferee is id of the leader transfer target when its value is not
  // zero. Follow the procedure defined in raft thesis 3.10.
  uint64_t leadTransferee_{0};
  // Only one conf change may be pending (in the log, but not yet
  // applied) at a time. This is enforced via pendingConfIndex, which
  // is set to a value >= the log index of the latest pending
  // configuration change (if any). Config changes are only allowed to
  // be proposed if the leader's applied index is greater than this
  // value.
  uint64_t pendingConfIndex_{0};

  std::unique_ptr<ReadOnly> readOnly_;

  // number of ticks since it reached last electionTimeout when it is leader
  // or candidate.
  // number of ticks since it reached last electionTimeout or received a
  // valid message from current leader when it is a follower.
  int electionElapsed_{0};

  // number of ticks since it reached last heartbeatTimeout.
  // only leader keeps heartbeatElapsed.
  int heartbeatElapsed_{0};

  bool checkQuorum_{false};
  bool preVote_{false};

  int heartbeatTimeout_{0};
  int electionTimeout_{0};
  // randomizedElectionTimeout is a random number between
  // [electiontimeout, 2 * electiontimeout - 1]. It gets reset
  // when raft changes its state to follower or candidate.
  int randomizedElectionTimeout_{0};
  bool disableProposalForwarding_{false};

  // for mock test
  std::function<error_s(idl::Message &)> step_;

 public:
  static Raft *New(Config *c);

  bool hasLeader() const { return lead_ == 0; }

  SoftState softState() const {
    return SoftState().Lead(lead_).RaftState(state_);
  }

  idl::HardState hardState() const {
    return idl::HardState()
        .term(term_)
        .vote(vote_)
        .commit(raftLog_->committed_);
  }

  size_t quorum() const { return prs_.size() / 2 + 1; }

  std::vector<uint64_t> nodes() const {
    std::vector<uint64_t> nodes;
    for (const auto &p : prs_) {
      uint64_t id = p.first;
      nodes.push_back(id);
    }
    std::sort(nodes.begin(), nodes.end());
    return nodes;
  }

  std::vector<uint64_t> learnerNodes() const {
    std::vector<uint64_t> nodes;
    for (auto p : learnerPrs_) {
      uint64_t id = p.first;
      nodes.push_back(id);
    }
    std::sort(nodes.begin(), nodes.end());
    return nodes;
  }

  // send persists state to stable storage and then sends to its mailbox.
  void send(idl::Message m);

  std::shared_ptr<Progress> getProgress(uint64_t id) {
    auto it = prs_.find(id);
    if (it != prs_.end()) {
      return it->second;
    }
    auto it2 = learnerPrs_.find(id);
    if (it2 != learnerPrs_.end()) {
      return it2->second;
    }
    return nullptr;
  }

  // sendAppend sends an append RPC with new entries (if any) and the
  // current commit index to the given peer.
  void sendAppend(uint64_t to) {
    maybeSendAppend(to, true);
  }

  // maybeSendAppend sends an append RPC with new entries to the given peer,
  // if necessary. Returns true if a message was sent. The sendIfEmpty
  // argument controls whether messages with no entries will be sent
  // ("empty" messages are useful to convey updated Commit indexes, but
  // are undesirable when we're sending multiple messages in a batch).
  bool maybeSendAppend(uint64_t to, bool sendIfEmpty);

  // sendHeartbeat sends an empty MsgApp
  void sendHeartbeat(uint64_t to, std::string ctx);

  void forEachProgress(std::function<void(uint64_t, std::shared_ptr<Progress> &)> f);

  // bcastAppend sends RPC, with entries to all peers that are not up-to-date
  // according to the progress recorded in r.prs.
  void bcastAppend() {
    forEachProgress([this](uint64_t id, std::shared_ptr<Progress> &) {
      if (id == id_) {
        return;
      }
      sendAppend(id);
    });
  }

  // bcastHeartbeat sends RPC, without entries to all the peers.
  void bcastHeartbeat();

  void bcastHeartbeatWithCtx(std::string ctx) {
    forEachProgress([this, &ctx](uint64_t id, std::shared_ptr<Progress> &) {
      if (id == id_) {
        return;
      }
      sendHeartbeat(id, ctx);
    });
  }

  // maybeCommit attempts to advance the commit index. Returns true if
  // the commit index changed (in which case the caller should call
  // r.bcastAppend).
  bool maybeCommit();

  void reset(uint64_t term);

  void appendEntry(idl::EntryVec es);

  // tickElection is run by followers and candidates after r.electionTimeout.
  void tickElection() {
    electionElapsed_++;

    if (promotable() && electionElapsed_ >= randomizedElectionTimeout_) {
      electionElapsed_ = 0;
      Step(idl::Message().from(id_).type(idl::MsgHup));
    }
  }

  // tickHeartbeat is run by leaders to send a MsgBeat after r.heartbeatTimeout.
  void tickHeartbeat();

  void becomeFollower(uint64_t term, uint64_t lead);

  void becomeCandidate();

  void becomePreCandidate();

  void becomeLeader();

  void campaign(CampaignType t);

  size_t poll(uint64_t id, idl::MessageType t, bool v);

  error_s Step(idl::Message m) override;

  error_s stepLeader(idl::Message &m);

  // stepCandidate is shared by StateCandidate and StatePreCandidate; the difference is
  // whether they respond to MsgVoteResp or MsgPreVoteResp.
  error_s stepCandidate(idl::Message &);

  error_s stepFollower(idl::Message &);

  void handleAppendEntries(idl::Message &m);

  void handleHeartbeat(idl::Message &m) {
    raftLog_->commitTo(m.commit());
    send(idl::Message().to(m.from()).type(idl::MsgHeartbeatResp).context(m.context()));
  }

  void handleSnapshot(idl::Message &m);

  // restore recovers the state machine from a snapshot. It restores the log and
  // the configuration of state machine.
  bool restore(idl::Snapshot s);

  void restoreNode(std::vector<uint64_t> nodes, bool isLearner);

  int granted() const {
    int gr = 0;
    for (auto &e : votes_) {
      gr += e.second;
    }
    return gr;
  }

  // promotable indicates whether state machine can be promoted to leader,
  // which is true when its own id is in progress list.
  bool promotable() const { return prs_.find(id_) != prs_.end(); }

  void addNode(uint64_t id) { addNodeOrLearnerNode(id, false); }

  void addLearner(uint64_t id) { addNodeOrLearnerNode(id, true); }

  void addNodeOrLearnerNode(uint64_t id, bool isLearner);

  void removeNode(uint64_t id);

  void setProgress(uint64_t id, uint64_t match, uint64_t next, bool isLearner);

  void delProgress(uint64_t id) {
    prs_.erase(id);
    learnerPrs_.erase(id);
  }

  void loadState(idl::HardState state) {
    if (state.commit() < raftLog_->committed_ || state.commit() > raftLog_->lastIndex()) {
      RAFT_LOG(PANIC, "%x state.commit %d is out of range [%d, %d]", id_, state.commit(), raftLog_->committed_, raftLog_->lastIndex());
    }
    raftLog_->committed_ = state.commit();
    term_ = state.term();
    vote_ = state.vote();
  }

  // pastElectionTimeout returns true iff r.electionElapsed is greater
  // than or equal to the randomized election timeout in
  // [electiontimeout, 2 * electiontimeout - 1].
  bool pastElectionTimeout() const {
    return electionElapsed_ >= randomizedElectionTimeout_;
  }

  void resetRandomizedElectionTimeout() {
    randomizedElectionTimeout_ = electionTimeout_ + randIntn(electionTimeout_);
  }

  // checkQuorumActive returns true if the quorum is active from
  // the view of the local raft state machine. Otherwise, it returns
  // false.
  // checkQuorumActive also resets all RecentActive to false.
  bool checkQuorumActive();

  void sendTimeoutNow(uint64_t to) { send(idl::Message().to(to).type(idl::MsgTimeoutNow)); }

  void abortLeaderTransfer() { leadTransferee_ = kNone; }

  error_s step(idl::Message &m) {
    if (step_) {
      return step_(m);
    }
    switch (state_) {
      case StateType::kLeader:
        return stepLeader(m);
      case StateType::kFollower:
        return stepFollower(m);
      case StateType::kCandidate:
      case StateType::kPreCandidate:
        return stepCandidate(m);
      default:
        __builtin_unreachable();
    }
  }

  void tick() {
    switch (state_) {
      case StateType::kLeader:
        tickHeartbeat();
        break;
      case StateType::kFollower:
      case StateType::kCandidate:
      case StateType::kPreCandidate:
        tickElection();
        break;
      default:
        __builtin_unreachable();
    }
  }

  // Test util
  std::vector<idl::Message> readMessages() override {
    return std::move(msgs_);
  }

  // Test util
  std::string type() const override {
    return "raft";
  }

  // getStatus gets a copy of the current raft status.
  Status getStatus() const;

  ReadyUPtr newReady(const SoftState &prevSoftSt, const idl::HardState &prevHardSt);
};

using RaftUPtr = std::unique_ptr<Raft>;

inline size_t numOfPendingConf(idl::EntrySpan ents) {
  size_t n = 0;
  for (const auto &e : ents) {
    if (e.type() == idl::EntryConfChange) {
      n++;
    }
  }
  return n;
}

}  // namespace yaraft
