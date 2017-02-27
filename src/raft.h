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
#include "progress.h"
#include "raft_log.h"
#include "state_machine.h"

#include <fmt/format.h>
#include <glog/logging.h>

namespace yaraft {

class Raft : public StateMachine {
  enum StateRole { kFollower, kCandidate, kLeader };

  enum CampaignType {
    // kCampaignElection represents a normal (time-based) election (the second phase
    // of the election when Config.preVote is true).
    kCampaignElection,
  };

 public:
  Raft(Config* conf) : c_(conf) {
    LOG_ASSERT(conf->Validate());
    id_ = conf->id;
    step_ = std::bind(&Raft::stepImpl, this, std::placeholders::_1);
  }

  virtual Status Step(const pb::Message& m) override {
    if (currentTerm_ > m.term()) {
      // ignore the message

      LOG(INFO) << fmt::format(
          "{:x} [term: {:d}] ignored a {:s} message with lower term from {:x} [term: {:d}]", id_,
          currentTerm_, m.GetTypeName(), m.from(), m.term());
      return Status::OK();
    }

    if (currentTerm_ < m.term()) {
      uint64_t lead = m.from();
      if (m.type() == pb::MsgVote) {
        lead = 0;
      }

      // Any messages (even a vote, except for prevote) will cause current node to step down as
      // follower.
      LOG(INFO) << fmt::format(
          "%x [term: %d] received a %s message with higher term from %x [term: %d]", id_,
          currentTerm_, m.GetTypeName(), m.from(), m.term());

      becomeFollower(m.term(), lead);
    }

    DLOG_ASSERT(currentTerm_ <= m.term());

    switch (m.type()) {
      case pb::MsgHup:
        LOG_ASSERT(role_ == kFollower);
        LOG(INFO) << id_ << " is starting a new election at term " << currentTerm_;
        campaign(kCampaignElection);
        break;
      case pb::MsgVote:
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

        break;
      default:
        step_(m);
    }
    return Status::OK();
  }

  uint64_t Term() const {
    return currentTerm_;
  }

 private:
  void becomeFollower(uint64_t term, uint64_t lead) {
    role_ = kFollower;
    currentLeader_ = lead;
    currentTerm_ = term;

    electionElapsed_ = 0;
    votedFor_ = 0;

    LOG(INFO) << id_ << " became follower at term " << currentTerm_;
  }

  void becomeCandidate() {
    DLOG_ASSERT(role_ != kLeader) << "invalid transition [leader -> candidate]";
    role_ = kCandidate;
    votedFor_ = id_;  // vote for itself
    LOG(INFO) << id_ << " became candidate at term " << currentTerm_;
  }

  void becomeLeader() {
    DLOG_ASSERT(role_ != kFollower) << "invalid transition [follower -> leader]";
    DLOG_ASSERT(role_ != kLeader) << fmt::format("%x has already a leader", id_);

    role_ = kLeader;
    heartbeatElapsed_ = 0;
    LOG(INFO) << id_ << " became leader at term " << currentTerm_;
  }

  void stepLeader(const pb::Message& m) {
    switch (m.type()) {
      case pb::MsgBeat:
        bcastHeartbeat();
        return;
      default:
        break;
    }

    DLOG_ASSERT(prs_.find(m.to()) == prs_.end())
        << fmt::format("%x no progress available for %x", id_, m.from());

    auto& pr = prs_[m.to()];

    switch (m.type()) {
      case pb::MsgAppResp:
        if (m.reject()) {
          DLOG(INFO) << fmt::format(
              "%x received msgApp rejection(lastindex: %d) from %x for index %d", id_,
              m.rejecthint(), m.from(), m.index());

          if (pr.MaybeDecrTo(m.index(), m.rejecthint())) {
            // retry with a smaller index
            DLOG(INFO) << fmt::format("%x decreased progress of %x to [%s]", id_, m.from(),
                                      pr.ToString());
            sendAppend(m.from());
          }
        } else {
          if (pr.MaybeUpdate(m.index())) {
          }
        }
        break;
      case pb::MsgHeartbeatResp:
        break;
    }
  }

  void stepCandidate(const pb::Message& m) {}

  void stepFollower(const pb::Message& m) {
    switch (m.type()) {
      case pb::MsgApp:
        electionElapsed_ = 0;
        currentLeader_ = m.from();
        handleAppendEntries(m);
        break;
    }
  }

  void stepImpl(const pb::Message& m) {
    switch (role_) {
      case kLeader:
        stepLeader(m);
        break;
      case kCandidate:
        stepCandidate(m);
        break;
      case kFollower:
        stepFollower(m);
    }
  }

  void loadState(pb::HardState state);
  pb::HardState hardState() const;

  void _tick() {
    switch (role_) {
      case kLeader:
        tickHeartbeat();
        break;
      case kFollower:
      case kCandidate:
        tickElection();
    }
  }

  void tickHeartbeat() {
    heartbeatElapsed_++;

    if (heartbeatElapsed_ >= c_->heartbeatTick) {
      heartbeatElapsed_ = 0;

      pb::Message msg;
      msg.set_from(id_);
      msg.set_type(pb::MsgBeat);
      Step(msg);
    }
  }

  void tickElection() {
    electionElapsed_++;

    if (promotable() && electionElapsed_ >= randomizedElectionTimeout_) {
      electionElapsed_ = 0;

      pb::Message msg;
      msg.set_from(id_);
      msg.set_type(pb::MsgHup);
      Step(msg);
    }
  }

  void send(pb::Message& m) {
    m.set_from(id_);

    if (m.type() == pb::MsgVote) {
      DLOG_ASSERT(m.term() != 0) << fmt::format("term should be set when sending %s",
                                                m.GetTypeName());
    } else {
      DLOG_ASSERT(m.term() == 0) << fmt::format("term should not be set when sending %s (was %d)",
                                                m.GetTypeName(), m.term());
    }

    mails_.push_back(std::move(m));
  }

  void sendVoteResp(const pb::Message& m, bool reject) {
    LOG(INFO) << fmt::format(
        "%x [logterm: %d, index: %d, vote: %x] cast %s for %x [logterm: %d, index: %d] at term %d",
        id_, log_->LastTerm(), log_->LastIndex(), votedFor_, m.GetTypeName(), m.from(), m.logterm(),
        m.index(), currentTerm_);

    pb::Message msgToBeSent;
    msgToBeSent.set_reject(reject);
    msgToBeSent.set_to(m.from());
    msgToBeSent.set_type(pb::MsgVote);
    send(msgToBeSent);
  }

  // promotable indicates whether state machine can be promoted to leader,
  // which is true when its own id is in progress list.
  bool promotable() const;

  void bcastHeartbeat() {
    for (const auto& e : prs_) {
      if (id_ == e.first)
        continue;
      sendHeartbeat(id_);
    }
  }

  void bcastAppend() {
    for (const auto& e : prs_) {
      if (id_ == e.first)
        continue;
      sendAppend(id_);
    }
  }

  void sendAppend(uint64_t to) {
    const auto& pr = prs_[to];

    pb::Message m;
    m.set_to(to);

    uint64_t prevLogIndex = pr.NextIndex() - 1;
    auto s = log_->Term(prevLogIndex);
    if (s.OK()) {
      uint64_t prevLogTerm = s.GetValue();
      m.set_type(pb::MsgApp);
      m.set_index(prevLogIndex);
      m.set_term(prevLogTerm);
    } else {
    }
    send(m);
  }

  void sendHeartbeat(uint64_t to) {
    // Attach the commit as min(to.matched, raftlog.committed).
    // When the leader sends out heartbeat message,
    // the receiver(follower) might not be matched with the leader
    // or it might not have all the committed entries.
    // The leader MUST NOT forward the follower's commit to
    // an unmatched index, in order to preserving Log Matching Property.

    pb::Message msgToBeSent;
    msgToBeSent.set_to(to);
    msgToBeSent.set_type(pb::MsgHeartbeat);
    msgToBeSent.set_commit(std::min(prs_[to].MatchIndex(), log_->Committed()));
    send(msgToBeSent);
  }

  void handleMsgHeartbeatResp(const pb::Message& m) {}

  void handleMsgAppResp(const pb::Message& m) {}

  void handleAppendEntries(const pb::Message& m) {}

  void handleHeartbeat(const pb::Message& m) {}

  void campaign(CampaignType type) {
    DLOG_ASSERT(type == kCampaignElection);

    for (const auto& e : prs_) {
      uint64_t pr_id = e.first;

      pb::Message msgToBeSent;
      msgToBeSent.set_term(currentTerm_);
      msgToBeSent.set_to(pr_id);
      msgToBeSent.set_type(pb::MsgVote);
      msgToBeSent.set_index(log_->LastIndex());
      msgToBeSent.set_term(log_->LastTerm());
      send(msgToBeSent);
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

 public:
  // For unit tests to mock step_.
  std::function<void(const pb::Message&)> step_;

  void TEST_SetTerm(uint64_t term) {
    currentTerm_ = term;
  }

 private:
  uint64_t id_;

  // Number of ticks since it reached last electionTimeout when it is leader or candidate.
  // Number of ticks since it reached last electionTimeout or received a valid message from
  // current leader when it is a follower.
  // electionElapsed is used for CheckQuorum machanism when it's leader.
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
  uint64_t votedFor_;  // who we voted for

  const std::unique_ptr<const Config> c_;

  // msgs to be sent are temporarily stored in MailBox.
  using MailBox = std::vector<pb::Message>;
  MailBox mails_;

  // peer id -> Progress
  using PeerMap = std::unordered_map<uint64_t, Progress>;
  PeerMap prs_;
};

}  // namespace yaraft
