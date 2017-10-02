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
#include "fluent_pb.h"
#include "logging.h"
#include "pb_utils.h"
#include "progress.h"
#include "raft_log.h"

namespace yaraft {

inline pb::MessageType voteRespType(pb::MessageType voteType) {
  return voteType == pb::MsgVote ? pb::MsgVoteResp : pb::MsgPreVoteResp;
}

class Raft {
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
      : c_(conf),
        id_(conf->id),
        log_(new RaftLog(conf->storage)),
        electionElapsed_(0),
        votedFor_(0),
        pendingConf_(false) {
    step_ = std::bind(&Raft::stepImpl, this, std::placeholders::_1);

    pb::HardState hardState;
    auto s = c_->storage->InitialState(&hardState, nullptr);
    if (!s) {
      LOG(FATAL, s.ToString());
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

    FMT_SLOG(
        INFO,
        "newRaft %x [peers: [%s], term: %d, commit: %d, applied: %d, lastindex: %d, lastterm: %d]",
        id_, nodeStr, currentTerm_, log_->CommitIndex(), log_->LastApplied(), log_->LastIndex(),
        log_->LastTerm());
  }

  Status Step(pb::Message& m) {
    if (m.term() == 0) {
      // local message
    } else if (currentTerm_ > m.term()) {
      // ignore the message
      FMT_SLOG(INFO, "%x [term: %d] ignored a %s message with lower term from %x [term: %d]", id_,
               currentTerm_, pb::MessageType_Name(m.type()), m.from(), m.term());
      return Status::OK();
    } else if (currentTerm_ < m.term()) {
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

        FMT_SLOG(INFO, "%x [term: %d] received a %s message with higher term from %x [term: %d]",
                 id_, currentTerm_, pb::MessageType_Name(m.type()), m.from(), m.term());

        becomeFollower(m.term(), lead);
      }
    }

    switch (m.type()) {
      case pb::MsgHup:
        DLOG_ASSERT(role_ != kLeader);
        FMT_SLOG(INFO, "%x is starting a new election at term %d", id_, currentTerm_);
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

  bool IsLeader() const {
    return role_ == kLeader;
  }

  void Tick() {
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

  bool HasPeer(uint64_t id) const {
    return (prs_.find(id) != prs_.end());
  }

  std::set<uint64_t> Peers() const {
    std::set<uint64_t> peers;
    for (auto& e : prs_) {
      peers.insert(e.first);
    }
    return peers;
  }

  // call this function when a new ConfChangeAddNode has applied.
  void AddNode(uint64_t nodeId) {
    pendingConf_ = false;

    // Ignore any redundant addNode calls (which can happen because the
    // initial bootstrapping entries are applied twice).
    if (prs_.find(nodeId) != prs_.end()) {
      return;
    }

    prs_[nodeId] = Progress().MatchIndex(0).NextIndex(log_->LastIndex() + 1);
  }

  void RemoveNode(uint64_t nodeId) {
    pendingConf_ = false;

    if (prs_.find(nodeId) != prs_.end()) {
      prs_.erase(nodeId);
    }
  }

 private:
  void becomeFollower(uint64_t term, uint64_t lead) {
    role_ = kFollower;
    currentLeader_ = lead;
    currentTerm_ = term;

    votedFor_ = 0;
    resetRandomizedElectionTimeout();

    FMT_SLOG(INFO, "%x became follower at term %d", id_, currentTerm_);
  }

  void becomePreCandidate() {
    role_ = kPreCandidate;
    currentLeader_ = 0;

    // Becoming a pre-candidate changes our state,
    // but doesn't change anything else. In particular it does not increase
    // currentTerm_ or change votedFor.
    FMT_SLOG(INFO, "%x became pre-candidate at term %d", id_, currentTerm_);
  }

  void becomeCandidate() {
    if (role_ == kLeader) {
#ifdef BUILD_TESTS
      throw RaftError("invalid transition [leader -> candidate]");
#else
      FMT_SLOG(FATAL, "invalid transition [leader -> candidate]");
#endif
    }

    role_ = kCandidate;
    FMT_SLOG(INFO, "%x became candidate at term %d", id_, currentTerm_);

    votedFor_ = id_;

    currentTerm_++;
    currentLeader_ = 0;
    resetRandomizedElectionTimeout();
  }

  // number of uncommitted conf change entries
  size_t numOfPendingConf() {
    auto s = log_->Entries(log_->CommitIndex() + 1, UINT64_MAX);
    FATAL_NOT_OK(s, "Log::Entries");
    EntryVec& ents = s.GetValue();

    size_t n = 0;
    for (auto& e : ents) {
      if (e.type() == pb::EntryConfChange) {
        n++;
      }
    }
    return n;
  }

  void becomeLeader() {
    if (role_ == kFollower) {
#ifdef BUILD_TESTS
      throw RaftError("invalid transition [follower -> leader]");
#else
      FMT_SLOG(FATAL, "invalid transition [follower -> leader]");
#endif
    }

    role_ = kLeader;
    currentLeader_ = id_;
    heartbeatElapsed_ = 0;

    size_t nconf = numOfPendingConf();
    if (nconf > 1) {
#ifdef BUILD_TESTS
      throw RaftError("unexpected multiple uncommitted config entry");
#else
      FMT_LOG(FATAL, "unexpected multiple uncommitted config entry");
#endif
    }
    if (nconf == 1) {
      pendingConf_ = true;
    }

    prs_.clear();
    for (uint64_t id : c_->peers) {
      prs_[id] = Progress().NextIndex(log_->LastIndex() + 1).MatchIndex(0);
    }
    prs_[id_].MatchIndex(log_->LastIndex());

    FMT_SLOG(INFO, "%x became leader at term %d", id_, currentTerm_);
  }

  void stepLeader(pb::Message& m) {
    switch (m.type()) {
      case pb::MsgBeat:
        bcastHeartbeat();
        return;
      case pb::MsgProp:
        handleMsgProp(m);
        return;
      default:
        break;
    }

    if (UNLIKELY(prs_.find(m.from()) == prs_.end())) {
      FMT_SLOG(FATAL, "%x no progress available for %x", id_, m.from());
    }

    switch (m.type()) {
      case pb::MsgAppResp:
        handleMsgAppResp(m);
        break;
      case pb::MsgHeartbeatResp:
        handleMsgHeartbeatResp(m);
        break;
      case pb::MsgSnapStatus:
        handleMsgSnapStatus(m);
        break;
      case pb::MsgUnreachable:
        handleMsgUnreachable(m);
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
    bool rejected = false;

    // Reply false if last AppendEntries call was received less than election timeout ago.
    if (currentLeader_ != 0 && electionElapsed_ < randomizedElectionTimeout_) {
      rejected = true;
    }

    // Reply false if term < currentTerm
    if (m.term() < currentTerm_) {
      rejected = true;
    }

    // If candidate's log is at least as up­to­date as receiver's log, grant vote
    if (log_->IsUpToDate(m.index(), m.logterm())) {
    } else {
      rejected = true;
    }

    sendVoteResp(m, rejected);
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
      FMT_SLOG(INFO, "%x received %s rejection from %x at term %d", id_,
               pb::MessageType_Name(m.type()), m.from(), currentTerm_);
    } else {
      FMT_SLOG(INFO, "%x received %s from %x at term %d", id_, pb::MessageType_Name(m.type()),
               m.from(), currentTerm_);
    }

    voteGranted_[m.from()] = !m.reject();

    int gr = granted();
    FMT_SLOG(INFO, "%x [quorum:%d] has received %d %s votes and %d vote rejections", id_, quorum(),
             gr, pb::MessageType_Name(m.type()), voteGranted_.size() - gr);

    if (gr >= quorum()) {
      if (m.type() == pb::MsgVoteResp) {
        becomeLeader();
        appendRawEntries(PBMessage().Entries({PBEntry().Data(nullptr).v}).v);
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

      // If a candidate receives an AppendEntries RPC from another rpc claiming
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
      case pb::MsgSnap:
        becomeFollower(m.term(), m.from());
        handleSnapshot(m);
        break;

      default:
        break;
    }
  }

  void stepFollower(pb::Message& m) {
    switch (m.type()) {
      case pb::MsgApp:
        handleAppendEntries(m);
        break;
      case pb::MsgHeartbeat:
        handleHeartbeat(m);
        break;
      case pb::MsgSnap:
        handleSnapshot(m);
        break;
      default:
        // ignored
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
    if (UNLIKELY(state.commit() > log_->LastIndex() || state.commit() < log_->CommitIndex())) {
      D_FMT_SLOG(FATAL, "%x state.commit %d is out of range [%d, %d]", id_, state.commit(),
                 log_->CommitIndex(), log_->LastIndex());
    }
    currentTerm_ = state.term();
    votedFor_ = state.vote();
    log_->CommitTo(state.commit());
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
      // All {pre-,}campaign messages need to have the term set when
      // sending.
      // - MsgVote: m.Term is the term the node is campaigning for,
      //   non-zero as we increment the term when campaigning.
      // - MsgPreVote: m.Term is the term the node will campaign,
      //   non-zero as we use m.Term to indicate the next term we'll be
      //   campaigning for
      if (UNLIKELY(m.term() == 0)) {
        FMT_SLOG(FATAL, "term should be set when sending %s", pb::MessageType_Name(m.type()));
      }
    } else {
      if (UNLIKELY(m.term() != 0)) {
        FMT_SLOG(FATAL, "term should not be set when sending %s (was %d)",
                 pb::MessageType_Name(m.type()), m.term());
      }

      m.set_term(currentTerm_);
    }
    mails_.push_back(std::move(m));
  }

  void sendVoteResp(const pb::Message& m, bool reject) {
    if (reject) {
      FMT_SLOG(INFO,
               "%x [logterm: %d, index: %d, vote: %x] rejected %s from %x [logterm: %d, index: %d] "
               "at term %d",
               id_, log_->LastTerm(), log_->LastIndex(), votedFor_, pb::MessageType_Name(m.type()),
               m.from(), m.logterm(), m.index(), currentTerm_);
    } else {
      FMT_SLOG(INFO,
               "%x [logterm: %d, index: %d, vote: %x] cast %s for %x [logterm: %d, index: %d] at "
               "term %d",
               id_, log_->LastTerm(), log_->LastIndex(), votedFor_, pb::MessageType_Name(m.type()),
               m.from(), m.logterm(), m.index(), currentTerm_);
    }

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

  // REQUIRED: `to` is an valid peer.
  void sendAppend(uint64_t to) {
    auto& pr = prs_[to];
    if (pr.IsPaused()) {
      return;
    }

    PBMessage m;
    m.To(to);

    uint64_t prevLogIndex = pr.NextIndex() - 1;
    auto sTerm = log_->Term(prevLogIndex);
    auto sEnts = log_->Entries(pr.NextIndex(), c_->maxSizePerMsg);

    if (sTerm.IsOK() && sEnts.IsOK()) {
      uint64_t prevLogTerm = sTerm.GetValue();
      m.Entries(sEnts.GetValue());
      m.Type(pb::MsgApp).Index(prevLogIndex).LogTerm(prevLogTerm).Commit(log_->CommitIndex());

      if (!m.v.entries().empty()) {
        switch (pr.State()) {
          case Progress::StateProbe: {
            pr.Pause();
            break;
          }
          case Progress::StateReplicate: {
            // optimistically increase the next when in ProgressStateReplicate
            uint64_t last = m.v.entries().rbegin()->index();
            pr.OptimisticUpdate(last);
            break;
          }
          default: {
            D_FMT_SLOG(FATAL, "%x is sending append in unhandled state %s", id_, pr.State());
            break;
          }
        }
      }
    } else {
      // send snapshot if we failed to get term or entries

      if (!pr.RecentActive()) {
        D_FMT_SLOG(INFO, "ignore sending snapshot to %x since it is not recently active", to);
        return;
      }

      pb::Snapshot snap = log_->Snapshot().GetValue();
      if (IsEmptySnapshot(snap)) {
        FMT_SLOG(FATAL,
                 "%x failed to send snapshot to %x because snapshot is temporarily unavailable",
                 id_, to);
      }

      D_FMT_SLOG(INFO,
                 "%x [firstIndex: %d, commit: %d] sent snapshot[index: %d, term: %d] to %x [%s]",
                 id_, log_->FirstIndex(), log_->CommitIndex(), snap.metadata().index(),
                 snap.metadata().term(), to, pr.ToString());

      pr.BecomeSnapshot(snap.metadata().index());
      D_FMT_SLOG(INFO, "%x paused sending replication messages to %x [%s]", id_, to, pr.ToString());

      m.Type(pb::MsgSnap).Snapshot(snap);
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

  void handleMsgHeartbeatResp(const pb::Message& m) {
    auto& pr = prs_[m.from()];
    pr.Resume();

    if (pr.MatchIndex() < log_->LastIndex()) {
      sendAppend(m.from());
    }
  }

  void handleMsgAppResp(const pb::Message& m) {
    auto& pr = prs_[m.from()];
    pr.RecentActive(true);

    if (m.reject()) {
      FMT_SLOG(DEBUG, "%x received msgApp rejection(lastindex: %d) from %x for index %d", id_,
               m.rejecthint(), m.from(), m.index());

      if (pr.MaybeDecrTo(m.index(), m.rejecthint())) {
        // resume the progress, retry with a lower index.
        FMT_SLOG(DEBUG, "%x decreased progress of %x to [%s]", id_, m.from(), pr.ToString());
        pr.Resume();
        if (pr.State() == Progress::StateReplicate) {
          pr.State(Progress::StateProbe);
        }
        sendAppend(m.from());
      }
    } else {
      if (pr.MaybeUpdate(m.index())) {
        if (maybeCommit()) {
          bcastAppend();
        }

        if (pr.State() == Progress::StateSnapshot && pr.NeedSnapshotAbort()) {
          D_FMT_SLOG(INFO, "%x snapshot aborted, resumed sending replication messages to %x [%s]",
                     id_, m.from(), pr.ToString());
          pr.BecomeProbe();
        } else if (pr.State() == Progress::StateProbe) {
          pr.BecomeReplicate();
        }
      }
    }
  }

  void handleHeartbeat(const pb::Message& m) {
    DLOG_ASSERT(role_ != StateRole::kLeader);

    currentLeader_ = m.from();
    electionElapsed_ = 0;

    log_->CommitTo(m.commit());
    send(PBMessage().To(m.from()).Type(pb::MsgHeartbeatResp).v);
  }

  void handleAppendEntries(pb::Message& m) {
    DLOG_ASSERT(role_ != StateRole::kLeader);

    currentLeader_ = m.from();
    electionElapsed_ = 0;

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

  // REQUIRED: current role is leader.
  void handleMsgProp(pb::Message& m) {
    if (UNLIKELY(m.entries().empty())) {
      FMT_SLOG(FATAL, "%x stepped empty MsgProp", id_);
    }

    if (UNLIKELY(m.entries().size() > 1)) {
      FMT_SLOG(FATAL, "%x: proposing multiple entries is not allowed", id_);
    }

    pb::Entry& e = *m.mutable_entries(0);
    if (e.type() == pb::EntryConfChange) {
      if (!pendingConf_) {
        pendingConf_ = true;
      } else {
        FMT_SLOG(INFO, "propose conf %s ignored since pending unapplied configuration",
                 e.DebugString());
        e = PBEntry().Type(pb::EntryNormal).v;
      }
    }

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

  // maybeCommit attempts to advance the commit index. Returns true if
  // the commit index changed (in which case the caller should call
  // r.bcastAppend).
  bool maybeCommit() {
    uint64_t oldCommit = log_->CommitIndex();
    advanceCommitIndex();
    return log_->CommitIndex() > oldCommit;
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

      FMT_SLOG(INFO, "%x [logterm: %d, index: %d] sent %s request to %x at term %d", id_,
               log_->LastTerm(), log_->LastIndex(), pb::MessageType_Name(voteType), peer_id, term);
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

  // restore recovers the state machine from a snapshot. It restores the log and the
  // configuration of state machine.
  // @returns true if restore success
  bool restore(pb::Snapshot& snap) {
    if (snap.metadata().index() <= log_->CommitIndex()) {
      return false;
    }

    StatusWith<uint64_t> sw = log_->Term(snap.metadata().index());
    if (sw.IsOK() && sw.GetValue() == snap.metadata().term()) {
      FMT_SLOG(INFO,
               "%x [commit: %d, lastindex: %d, lastterm: %d] fast-forwarded commit to snapshot "
               "[index: %d, term: %d]",
               id_, log_->CommitIndex(), log_->LastIndex(), log_->LastTerm(),
               snap.metadata().index(), snap.metadata().term());

      log_->CommitTo(snap.metadata().index());
      return false;
    }

    FMT_SLOG(INFO,
             "%x [commit: %d, lastindex: %d, lastterm: %d] starts to restore snapshot [index: %d, "
             "term: %d]",
             id_, log_->CommitIndex(), log_->LastIndex(), log_->LastTerm(), snap.metadata().index(),
             snap.metadata().term());

    auto& tmpPbNodes = snap.metadata().conf_state().nodes();
    std::vector<uint64_t> nodes(tmpPbNodes.begin(), tmpPbNodes.end());

    // apply snapshot only when there's no existing log entry with the same index and term as
    // Snapshot.LastIndex and Snapshot.LastTerm.
    // the snapshot will clear the entries in raftLog
    log_->Restore(snap);

    prs_.clear();
    for (uint64_t n : nodes) {
      uint64_t match = 0, next = log_->LastIndex() + 1;
      if (n == id_) {
        match = next - 1;
      }
      prs_[n].MatchIndex(match).NextIndex(next);
      FMT_SLOG(INFO, "%x restored progress of %x [%s]", id_, n, prs_[n].ToString());
    }

    return true;
  }

  void handleSnapshot(pb::Message& m) {
    uint64_t sindex = m.snapshot().metadata().index();
    uint64_t sterm = m.snapshot().metadata().term();
    if (restore(*m.mutable_snapshot())) {
      FMT_SLOG(INFO, "%x [commit: %d] restored snapshot [index: %d, term: %d]", id_,
               log_->CommitIndex(), sindex, sterm);
      send(PBMessage().Type(pb::MsgAppResp).To(m.from()).Index(log_->CommitIndex()).v);
    } else {
      FMT_SLOG(INFO, "%x [commit: %d] ignored snapshot [index: %d, term: %d]", id_,
               log_->CommitIndex(), sindex, sterm);
      send(PBMessage().Type(pb::MsgAppResp).To(m.from()).Index(log_->CommitIndex()).v);
    }
  }

  void handleMsgSnapStatus(pb::Message& m) {
    Progress& pr = prs_[m.from()];
    if (pr.State() != Progress::StateSnapshot) {
      return;
    }

    if (!m.reject()) {
      pr.BecomeProbe();
      D_FMT_SLOG(INFO, "%x snapshot succeeded, resumed sending replication messages to %x [%s]",
                 id_, m.from(), pr.ToString());
    } else {
      pr.SnapshotFailure();
      pr.BecomeProbe();
      D_FMT_SLOG(INFO, "%x snapshot failed, resumed sending replication messages to %x [%s]", id_,
                 m.from(), pr.ToString());
    }

    // If snapshot finish, wait for the msgAppResp from the remote node before sending
    // out the next msgApp.
    // If snapshot failure, wait for a heartbeat interval before next try
    pr.Pause();
  }

  void handleMsgUnreachable(pb::Message& m) {
    Progress& pr = prs_[m.from()];

    // During optimistic replication, if the remote becomes unreachable,
    // there is huge probability that a MsgApp is lost.
    if (pr.State() == Progress::StateReplicate) {
      pr.BecomeProbe();
    }
    D_FMT_SLOG(INFO, "%x failed to send message to %x because it is unreachable [%s]", id_,
               m.from(), pr.ToString());
  }

 private:
  friend class RaftTest;
  friend class RaftPaperTest;
  friend class Network;
  friend class RawNode;

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

  bool pendingConf_;

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
