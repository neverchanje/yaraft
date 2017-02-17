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

#include <glog/logging.h>

namespace yaraft {

// TODO: Extract Raft as an simple interface with only Raft::Step.
class Raft {
  enum StateRole { kFollower, kCandidate, kLeader };

  enum CampaignType {
    // kCampaignElection represents a normal (time-based) election (the second phase
    // of the election when Config.PreVote is true).
    kCampaignElection,
  };

 public:
  Raft(const Config& conf) : c_(&conf) {
    LOG_ASSERT(conf.Validate());
    id_ = conf.ID;
  }

  Status Step(const pb::Message& m) {
    if (term_ < m.term()) {
      uint64_t lead = m.from();
      if (m.type() == pb::MsgVote) {
        lead = 0;
      }

      // Any messages (even a vote, except for prevote) will cause current node to step down as
      // follower.
      LOG(INFO) << id_ << " [term: " << term_ << "] received a " << m.GetTypeName()
                << " message with higher term from " << m.index() << " [term: " << m.term() << "] ";
      becomeFollower(m.term(), lead);
    } else if (term_ > m.term()) {
      // ignore other cases
      LOG(INFO) << id_ << " [term: " << term_ << "] ignored a " << m.GetTypeName()
                << " message with lower term from " << m.index() << " [term: " << m.term() << "] ";
      return Status::OK();
    }

    DLOG_ASSERT(term_ <= m.term());

    switch (m.type()) {
      case pb::MsgHup:
        LOG_ASSERT(role_ != kLeader);
        LOG(INFO) << id_ << " is starting a new election at term " << term_;
        campaign(kCampaignElection);
        break;
      case pb::MsgVote:
      case pb::MsgPreVote:
        if ((vote_ == 0 || m.term() > term_ || vote_ == m.from()) &&
            log_->IsUpToDate(m.index(), m.logterm())) {
          // - If we haven't voted for any candidates,
          // - if this is a prevote, (for MsgVote m.term() should always equal term_)
          // - if we have voted for the same peer (repeated votes for a same peer is allowed),

          // then we can grant the vote.

          sendVoteResp(m);

          // for real votes
          if (m.type() == pb::MsgVote) {
            electionElapsed_ = 0;
            vote_ = m.from();
          }
        }

        break;
      default:
        _step(m);
    }
    return Status::OK();
  }

 private:
  void becomeFollower(uint64_t term, int lead) {
    role_ = kFollower;
    lead_ = lead;

    electionElapsed_ = 0;

    LOG(INFO) << id_ << " became follower at term " << term_;
  }

  void becomeCandidate() {
    DLOG_ASSERT(role_ != kLeader) << "invalid transition [leader -> candidate]";
    role_ = kCandidate;
    vote_ = id_;  // vote for itself
    LOG(INFO) << id_ << " became candidate at term " << term_;
  }

  void becomeLeader() {
    DLOG_ASSERT(role_ != kFollower) << "invalid transition [follower -> leader]";
    role_ = kLeader;

    heartbeatElapsed_ = 0;

    LOG(INFO) << id_ << " became leader at term " << term_;
  }

  void stepLeader(const pb::Message& m) {
    switch (m.type()) {
      case pb::MsgBeat:
        bcastHeartbeat();
        return;
      default:
        break;
    }

    auto& pr = prs_[m.to()];

    switch (m.type()) {
      case pb::MsgAppResp:
        if (m.reject()) {
          DLOG(INFO) << id_ << " received msgApp rejection(lastindex: " << m.rejecthint()
                     << ") from " << m.from() << " for index " << m.index();
          if (prs_[m.to()].MaybeDecrTo(m.index(), m.rejecthint())) {
          }
        } else {
        }
        break;
    }
  }

  void stepCandidate(const pb::Message& m);

  void stepFollower(const pb::Message& m);

  void _step(const pb::Message& m) {
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

    if (heartbeatElapsed_ >= c_->HeartbeatTick) {
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

    if (m.type() == pb::MsgVote || m.type() == pb::MsgPreVote) {
      DLOG_ASSERT(m.term() != 0);
    } else {
      // Term should not be manually set when message type is not Vote or PreVote.
      DLOG_ASSERT(m.term() == 0) << m.GetTypeName();

      if (m.type() != pb::MsgProp)
        m.set_term(term_);
    }

    mails_.push_back(std::move(m));
  }

  void sendVoteResp(const pb::Message& m) {
    DLOG_ASSERT(m.type() == pb::MsgVote || m.type() == pb::MsgPreVote);

    pb::Message msgToBeSent;
    msgToBeSent.set_to(m.from());

    pb::MessageType respType = m.type() == pb::MsgVote ? pb::MsgVoteResp : pb::MsgPreVoteResp;
    msgToBeSent.set_type(respType);

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

  // sendHeartbeat sends an empty MsgApp
  void sendHeartbeat(uint64_t to) {
    // Attach the commit as min(to.matched, raftlog.committed).
    // When the leader sends out heartbeat message,
    // the receiver(follower) might not be matched with the leader
    // or it might not have all the committed entries.
    // The leader MUST NOT forward the follower's commit to
    // an unmatched index, in order to preserving Log Matchiing Property.

    pb::Message msgToBeSent;
    msgToBeSent.set_to(to);
    msgToBeSent.set_type(pb::MsgHeartbeat);
    msgToBeSent.set_commit(std::min(prs_[to].MatchIndex(), log_->Committed()));
    send(msgToBeSent);
  }

  void addNode(uint64_t id) {
    LOG_ASSERT(prs_.find(id) != prs_.end());
  }

  void campaign(CampaignType type) {
    DLOG_ASSERT(type == kCampaignElection);

    for (const auto& e : prs_) {
      uint64_t pr_id = e.first;

      pb::Message msgToBeSent;
      msgToBeSent.set_type(pb::MsgVote);
      msgToBeSent.set_term(term_);
      send(msgToBeSent);
    }
  }

  void resetRandomizedElectionTimeout() {
    static unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    static std::default_random_engine engine(seed);
    static std::uniform_int_distribution<int> rand(c_->ElectionTick, 2 * c_->ElectionTick - 1);
    randomizedElectionTimeout_ = rand(engine);
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
  // [ElectionTick, 2 * ElectionTick). It gets reset
  // when raft changes its state to follower or candidate.
  int randomizedElectionTimeout_;

  int lead_;
  std::unique_ptr<RaftLog> log_;

  StateRole role_;

  uint64_t term_;
  uint64_t vote_;  // who we voted for

  const Config* const c_;

  // msgs to be sent are temporarily stored in MailBox.
  using MailBox = std::vector<pb::Message>;
  MailBox mails_;

  using PeerMap = std::unordered_map<uint64_t, Progress>;
  PeerMap prs_;  // id_ is also included
};

}  // namespace yaraft
