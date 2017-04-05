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
#include <random>
#include <unordered_map>

#include "conf.h"
#include "exception.h"
#include "pb_helper.h"
#include "progress.h"
#include "raft_log.h"
#include "state_machine.h"

#include <fmt/format.h>
#include <glog/logging.h>

namespace yaraft {

inline pb::MessageType voteRespType(pb::MessageType voteType) {
  return voteType == pb::MsgVote ? pb::MsgVoteResp : pb::MsgPreVoteResp;
}

class Raft : public StateMachine {
  enum CampaignType {
    // kCampaignElection represents a normal (time-based) election (the second phase
    // of the election when Config.preVote is true).
    kCampaignElection,

    // kCampaignPreElection represents the first phase of a normal election when
    // Config.PreVote is true.
    kCampaignPreElection,
  };

 public:
  enum StateRole { kFollower, kCandidate, kPreCandidate, kLeader, kStateNum };

  explicit Raft(Config* conf)
      : c_(conf), log_(new RaftLog(conf->storage)), electionElapsed_(0), votedFor_(0) {
    LOG_ASSERT(conf->Validate());

    id_ = conf->id;
    step_ = std::bind(&Raft::stepImpl, this, std::placeholders::_1);

    pb::HardState hardState;
    auto s = c_->storage->InitialState(&hardState, nullptr);
    if (!s) {
      LOG(FATAL) << s;
    }
    loadState(hardState);

    becomeFollower(currentTerm_, 0);

    LOG_ASSERT(!conf->peers.empty());
    const auto& peers = conf->peers;
    for (uint64_t p : peers) {
      prs_[p] = Progress();
    }

    std::string nodeStr = std::to_string(*peers.begin());
    std::for_each(std::next(peers.begin()), peers.end(),
                  [&](uint64_t p) { nodeStr += ", " + std::to_string(p); });

    LOG(INFO) << fmt::format(
        "newRaft {:x} [peers: [{:s}], term: {:d}, commit: {:d}, applied: {:d}, lastindex: {:d}, "
        "lastterm: {:d}]",
        id_, nodeStr, currentTerm_, log_->CommitIndex(), log_->LastApplied(), log_->LastIndex(),
        log_->LastTerm());
  }

  Status Step(pb::Message& m) override {
    if (currentTerm_ > m.term()) {
      // ignore the message
      LOG(INFO) << fmt::format(
          "{:x} [term: {:d}] ignored a {:s} message with lower term from {:x} [term: {:d}]", id_,
          currentTerm_, pb::MessageType_Name(m.type()), m.from(), m.term());
      return Status::OK();
    }

    if (currentTerm_ < m.term()) {
      if (m.type() == pb::MsgPreVote) {
        // currentTerm never changes when receiving a PreVote.
      } else if (m.type() == pb::MsgPreVoteResp && !m.reject()) {
        // We send pre-vote requests with a term in our future. If the
        // pre-vote is granted, we will increment our term when we get a
        // quorum. If it is not, the term comes from the node that
        // rejected our vote so we should become a follower at the new
        // term.
      } else {
        uint64_t lead = m.from();
        if (m.type() == pb::MsgVote) {
          lead = 0;
        }

        LOG(INFO) << fmt::format(
            "{:x} [term: {:d}] received a {:s} message with higher term from {:x} [term: {:d}]",
            id_, currentTerm_, pb::MessageType_Name(m.type()), m.from(), m.term());

        becomeFollower(m.term(), lead);
      }
    }

    DLOG_ASSERT(currentTerm_ <= m.term());

    switch (m.type()) {
      case pb::MsgHup:
        DLOG_ASSERT(role_ != kLeader);
        LOG(INFO) << id_ << " is starting a new election at term " << currentTerm_;
        if (c_->preVote) {
          campaign(kCampaignPreElection);
        } else {
          campaign(kCampaignElection);
        }
        break;
      case pb::MsgVote:
        handleMsgVote(m);
        break;
      case pb::MsgPreVote:
        handleMsgPreVote(m);
        break;
      default:
        step_(m);
    }
    return Status::OK();
  }

  uint64_t Term() const {
    return currentTerm_;
  }

  uint64_t Id() const {
    return id_;
  }

 private:
  void becomeFollower(uint64_t term, uint64_t lead) {
    role_ = kFollower;
    currentLeader_ = lead;
    currentTerm_ = term;

    votedFor_ = 0;
    resetRandomizedElectionTimeout();

    LOG(INFO) << id_ << " became follower at term " << currentTerm_;
  }

  void becomePreCandidate() {
    role_ = kPreCandidate;

    // Becoming a pre-candidate changes our state,
    // but doesn't change anything else. In particular it does not increase
    // currentTerm_ or change votedFor.
    LOG(INFO) << id_ << " became pre-candidate at term " << currentTerm_;
  }

  void becomeCandidate() {
    if (role_ == kLeader)
      throw RaftError("invalid transition [leader -> candidate]");

    role_ = kCandidate;
    LOG(INFO) << id_ << " became candidate at term " << currentTerm_;

    votedFor_ = id_;

    currentTerm_++;
    currentLeader_ = 0;
    resetRandomizedElectionTimeout();
  }

  void becomeLeader() {
    if (role_ == kFollower)
      throw RaftError("invalid transition [follower -> leader]");

    role_ = kLeader;
    currentLeader_ = id_;
    heartbeatElapsed_ = 0;
    prs_.clear();

    for (uint64_t id : c_->peers) {
      prs_[id] = Progress().NextIndex(log_->LastIndex() + 1).MatchIndex(0);
    }
    prs_[id_].MatchIndex(log_->LastIndex());

    appendRawEntries(PBMessage().Entries({PBEntry().Data(nullptr).v}).v);

    LOG(INFO) << id_ << " became leader at term " << currentTerm_;
  }

  void stepLeader(pb::Message& m) {
    switch (m.type()) {
      case pb::MsgBeat:
        bcastHeartbeat();
        return;
      case pb::MsgProp:
        handleLeaderMsgProp(m);
        return;
      default:
        break;
    }

    DLOG_ASSERT(prs_.find(m.from()) != prs_.end())
        << fmt::format("{:x} no progress available for {:x}", id_, m.from());

    auto& pr = prs_[m.from()];

    switch (m.type()) {
      case pb::MsgAppResp:
        handleMsgAppResp(m);
        break;
      case pb::MsgHeartbeatResp:
        if (pr.MatchIndex() < log_->LastIndex()) {
          sendAppend(m.from());
        }
        break;
      default:
        // ignore unexpected messages
        break;
    }
  }

  int granted() const {
    int gr = 0;
    for (auto& e : voteGranted_) {
      gr += e.second;
    }
    return gr;
  }

  void handleMsgPreVote(const pb::Message& m) {
    // If a raft node receives a PreVote within the election timeout of hearing from a current
    // leader, it does not grants the pre-vote.
    if (currentLeader_ != 0 && electionElapsed_ < randomizedElectionTimeout_) {
      LOG(INFO) << fmt::format(
          "{:x} [logterm: {:d}, index: {:d}, vote: {:x}] ignored {:s} from {:x} [logterm: "
          "{:d}, index: {:d}] at term {:d}: lease is not expired (remaining ticks: {:d})",
          id_, log_->LastTerm(), log_->LastIndex(), votedFor_, pb::MessageType_Name(m.type()),
          m.from(), m.logterm(), m.index(), currentTerm_,
          randomizedElectionTimeout_ - electionElapsed_);
      return;
    }

    bool reject = true;
    if (m.term() > currentTerm_ && log_->IsUpToDate(m.index(), m.logterm())) {
      reject = false;
    }
    sendVoteResp(m, reject);
  }

  void handleMsgVote(const pb::Message& m) {
    if ((votedFor_ == 0 || votedFor_ == m.from()) && log_->IsUpToDate(m.index(), m.logterm())) {
      // - If we haven't voted for any candidates, or
      // - if we have voted for the same peer (repeated votes for a same candidate is allowed),
      // - and for all conditions above, the candidate's log must be at least as up-to-date as
      // the voter's (raft thesis 3.6).

      // then we can grant the vote.
      sendVoteResp(m, false);
      electionElapsed_ = 0;
      votedFor_ = m.from();
    } else {
      sendVoteResp(m, true);
    }
  }

  void handleMsgVoteResp(const pb::Message& m) {
    if (m.reject()) {
      LOG(INFO) << fmt::format("{:x} received {:s} rejection from {:x} at term {:d}", id_,
                               pb::MessageType_Name(m.type()), m.from(), currentTerm_);
    } else {
      LOG(INFO) << fmt::format("{:x} received {:s} from {:x} at term {:d}", id_,
                               pb::MessageType_Name(m.type()), m.from(), currentTerm_);
    }

    voteGranted_[m.from()] = !m.reject();

    int gr = granted();
    LOG(INFO) << fmt::format(
        "{:x} [quorum:{:d}] has received {:d} {:s} votes and {:d} vote rejections", id_, quorum(),
        gr, pb::MessageType_Name(m.type()), voteGranted_.size() - gr);

    if (gr >= quorum()) {
      if (m.type() == pb::MsgVoteResp) {
        becomeLeader();
        bcastAppend();
      } else {
        voteGranted_.clear();
        campaign(kCampaignElection);
      }
      return;
    }

    // return to follower state if it receives vote denial from a majority
    int rejected = static_cast<int>(voteGranted_.size()) - gr;
    if (rejected >= quorum()) {
      becomeFollower(m.term(), 0);
    }
  }

  // stepCandidate is shared by StateCandidate and StatePreCandidate; the difference is
  // whether they respond to MsgVoteResp or MsgPreVoteResp.
  void stepCandidate(pb::Message& m) {
    switch (m.type()) {
      // Only handle vote responses corresponding to our candidacy (while in
      // StateCandidate, we may get stale MsgPreVoteResp messages in this term from
      // our pre-candidate state).
      case pb::MsgPreVoteResp:
        if (role_ == kPreCandidate)
          handleMsgVoteResp(m);
        break;
      case pb::MsgVoteResp:
        if (role_ == kCandidate)
          handleMsgVoteResp(m);
        break;

      // If a candidate receives an AppendEntries RPC from another server claiming
      // to be leader whose term is at least as large as the candidate's current term,
      // it recognizes the leader as legitimate and returns to follower state.
      case pb::MsgApp:
        becomeFollower(m.term(), m.from());
        handleAppendEntries(m);
        break;
      case pb::MsgHeartbeat:
        becomeFollower(m.term(), m.from());
        handleHeartbeat(m);
        break;

      default:
        break;
    }
  }

  void stepFollower(pb::Message& m) {
    switch (m.type()) {
      case pb::MsgApp:
        currentLeader_ = m.from();
        handleAppendEntries(m);
        break;
    }
  }

  void stepImpl(pb::Message& m) {
    switch (role_) {
      case kLeader:
        stepLeader(m);
        break;
      case kCandidate:
      case kPreCandidate:
        stepCandidate(m);
        break;
      case kFollower:
        stepFollower(m);
      default:
        break;
    }
  }

  void loadState(pb::HardState state) {
    currentTerm_ = state.term();
    votedFor_ = state.vote();
  }

  void _tick() {
    switch (role_) {
      case kLeader:
        tickHeartbeat();
        break;
      case kFollower:
      case kCandidate:
      case kPreCandidate:
        tickElection();
        break;
      default:
        break;
    }
  }

  void tickHeartbeat() {
    heartbeatElapsed_++;

    if (heartbeatElapsed_ >= c_->heartbeatTick) {
      heartbeatElapsed_ = 0;
      bcastHeartbeat();
    }
  }

  void tickElection() {
    electionElapsed_++;

    if (promotable() && electionElapsed_ >= randomizedElectionTimeout_) {
      electionElapsed_ = 0;
      Step(PBMessage().From(id_).Type(pb::MsgHup).Term(currentTerm_).v);
    }
  }

  void send(pb::Message& m) {
    m.set_from(id_);

    if (m.type() == pb::MsgVote || m.type() == pb::MsgPreVote) {
      DLOG_ASSERT(m.term() != 0) << fmt::format(" term should be set when sending {:s}",
                                                pb::MessageType_Name(m.type()));
    } else {
      DLOG_ASSERT(m.term() == 0) << fmt::format(
          " term should not be set when sending {:s} (was {:d})", pb::MessageType_Name(m.type()),
          m.term());
      m.set_term(currentTerm_);
    }
    mails_.push_back(std::move(m));
  }

  void sendVoteResp(const pb::Message& m, bool reject) {
    LOG(INFO) << fmt::format(
        "{:x} [logterm: {:d}, index: {:d}, voteFor: {:x}] cast {:s} for {:x} [logterm: {:d}, "
        "index: "
        "{:d}] at term {:d}",
        id_, log_->LastTerm(), log_->LastIndex(), votedFor_, pb::MessageType_Name(m.type()),
        m.from(), m.logterm(), m.index(), currentTerm_);

    // send() will include term=currentTerm into message.
    send(PBMessage().Reject(reject).To(m.from()).Type(voteRespType(m.type())).v);
  }

  // promotable indicates whether state machine can be promoted to leader,
  // which is true when its own id is in progress list.
  bool promotable() const {
    return true;
  }

  void bcastHeartbeat() {
    for (const auto& e : prs_) {
      if (id_ == e.first)
        continue;
      sendHeartbeat(e.first);
    }
  }

  void bcastAppend() {
    for (const auto& e : prs_) {
      if (id_ == e.first)
        continue;
      sendAppend(e.first);
    }
  }

  void sendAppend(uint64_t to) {
    const auto& pr = prs_[to];

    PBMessage m;
    m.To(to);

    uint64_t prevLogIndex = pr.NextIndex() - 1;
    auto sTerm = log_->Term(prevLogIndex);

    if (sTerm.OK()) {
      uint64_t prevLogTerm = sTerm.GetValue();

      auto sEnts = log_->Entries(pr.NextIndex(), c_->maxSizePerMsg);
      LOG_ASSERT(sEnts.OK());
      m.Entries(sEnts.GetValue());

      m.Type(pb::MsgApp).Index(prevLogIndex).LogTerm(prevLogTerm).Commit(log_->CommitIndex());
    } else {
    }
    send(m.v);
  }

  void sendHeartbeat(uint64_t to) {
    // Attach the commit as min(to.matched, raftlog.committed).
    // When the leader sends out heartbeat message,
    // the receiver(follower) might not be matched with the leader
    // or it might not have all the committed entries.
    // The leader MUST NOT forward the follower's commit to
    // an unmatched index, in order to preserving Log Matching Property.

    auto m = PBMessage()
                 .To(to)
                 .Type(pb::MsgHeartbeat)
                 .Commit(std::min(prs_[to].MatchIndex(), log_->CommitIndex()));
    send(m.v);
  }

  void handleMsgHeartbeatResp(const pb::Message& m) {}

  void handleMsgAppResp(const pb::Message& m) {
    auto& pr = prs_[m.from()];
    if (m.reject()) {
      DLOG(INFO) << fmt::format(
          "{:x} received msgApp rejection(lastindex: {:d}) from {:x} for index {:d}", id_,
          m.rejecthint(), m.from(), m.index());

      if (pr.MaybeDecrTo(m.index(), m.rejecthint())) {
        // retry with a smaller index
        DLOG(INFO) << fmt::format("{:x} decreased progress of {:x} to [{:s}]", id_, m.from(),
                                  pr.ToString());
        sendAppend(m.from());
      }
    } else {
      if (pr.MaybeUpdate(m.index())) {
        advanceCommitIndex();
      }
    }
  }

  void handleAppendEntries(pb::Message& m) {
    DLOG_ASSERT(role_ == StateRole::kFollower);

    PBMessage msg;
    msg.To(m.from()).Type(pb::MsgAppResp);

    uint64_t newLastIndex = 0;
    if (log_->MaybeAppend(m, &newLastIndex)) {
      // commitIndex = min(leaderCommit, index of last new entry)
      log_->CommitTo(std::min(m.commit(), newLastIndex));
      send(msg.Index(newLastIndex).v);
    } else {
      send(msg.Index(m.index()).Reject().RejectHint(log_->LastIndex()).v);
    }
  }

  void handleLeaderMsgProp(pb::Message& m) {
    appendRawEntries(m);
    bcastAppend();
  }

  void appendRawEntries(pb::Message& m) {
    auto entries = m.mutable_entries();
    uint64_t li = log_->LastIndex(), i = 1;
    for (auto it = entries->begin(); it != entries->end(); it++) {
      it->set_term(currentTerm_);
      it->set_index(li + i);
      i++;
    }
    log_->Append(m.mutable_entries()->begin(), m.mutable_entries()->end());
    prs_[id_].MaybeUpdate(log_->LastIndex());
    advanceCommitIndex();
  }

  // advanceCommitIndex advances commitIndex to the largest index of log having
  // replicated on majority, except When leader's currentTerm is not equal to
  // term of the index (which means it's a new leader).
  void advanceCommitIndex() {
    DLOG_ASSERT(role_ == StateRole::kLeader);
    std::vector<uint64_t> matches;
    for (auto& e : prs_) {
      matches.push_back(e.second.MatchIndex());
    }
    std::sort(matches.begin(), matches.end(), std::greater<uint64_t>());

    uint64_t to = matches[quorum() - 1];
    if (log_->ZeroTermOnErrCompacted(to) == currentTerm_) {
      log_->CommitTo(to);
    }
  }

  void handleHeartbeat(const pb::Message& m) {
    DLOG_ASSERT(role_ == StateRole::kFollower);

    log_->CommitTo(m.commit());
    send(PBMessage().To(m.from()).Type(pb::MsgHeartbeatResp).v);
  }

  void campaign(CampaignType type) {
    DLOG_ASSERT(type == kCampaignElection || type == kCampaignPreElection);

    pb::MessageType voteType;
    uint64_t term = currentTerm_ + 1;
    if (type == kCampaignPreElection) {
      voteType = pb::MsgPreVote;
      becomePreCandidate();
    } else {
      voteType = pb::MsgVote;
      becomeCandidate();
    }

    // vote for itself
    Step(PBMessage().From(id_).To(id_).Term(term).Type(voteRespType(voteType)).v);

    auto m =
        PBMessage().Term(term).Type(voteType).Index(log_->LastIndex()).LogTerm(log_->LastTerm());
    for (const auto& e : prs_) {
      uint64_t peer_id = e.first;
      if (peer_id == id_)
        continue;

      LOG(INFO) << fmt::format("{:x} [logterm: {:d}, index: {:d}] sent {:s} to {:x} at term {:d}",
                               id_, log_->LastTerm(), log_->LastIndex(),
                               pb::MessageType_Name(voteType), peer_id, term);
      send(m.To(peer_id).v);
    }
  }

  int quorum() const {
    return static_cast<int>(prs_.size() / 2 + 1);
  }

  void resetRandomizedElectionTimeout() {
    static auto seed = std::chrono::system_clock::now().time_since_epoch().count();
    static std::default_random_engine engine(seed);
    static std::uniform_int_distribution<int> rand(c_->electionTick, 2 * c_->electionTick - 1);
    randomizedElectionTimeout_ = rand(engine);
  }

 private:
  friend class RaftTest;
  friend class RaftPaperTest;
  friend class Network;

  uint64_t id_;

  // Number of ticks since it reached last electionTimeout when it is leader or candidate.
  // Number of ticks since it reached last electionTimeout or received a valid message from
  // current leader when it is a follower.
  int electionElapsed_;

  // Number of ticks since it reached last heartbeatTimeout.
  // Only leader keeps heartbeatElapsed.
  int heartbeatElapsed_;

  // randomizedElectionTimeout is a random number between
  // [electionTick, 2 * electionTick). It gets reset
  // when raft changes its state to follower or candidate.
  int randomizedElectionTimeout_;

  uint64_t currentLeader_;
  std::unique_ptr<RaftLog> log_;

  StateRole role_;

  uint64_t currentTerm_;
  uint64_t votedFor_;

  std::unordered_map<uint64_t, bool> voteGranted_;

  std::unique_ptr<const Config> c_;

  // For unit tests to mock step_.
  std::function<void(pb::Message&)> step_;

  // msgs to be sent are temporarily stored in MailBox.
  using MailBox = std::vector<pb::Message>;
  MailBox mails_;

  // peer id -> Progress
  using PeerMap = std::unordered_map<uint64_t, Progress>;
  PeerMap prs_;
};

using RaftUPtr = std::unique_ptr<Raft>;

}  // namespace yaraft
