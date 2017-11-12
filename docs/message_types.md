    Copyright 2017 Wu Tao
    Copyright 2015 The etcd Authors
    
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at
    
        http://www.apache.org/licenses/LICENSE-2.0
    
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

# Message Types

Package raft sends and receives message in Protocol Buffer format (defined
in src/pb directory). Each state (follower, candidate, leader) implements its
own `stepImpl` method (`stepFollower`, `stepCandidate`, `stepLeader`) when
advancing with the given `yaraft::pb::Message`. Each step is determined by its
`yaraft::pb::MessageType`. Note that every step is checked by one common method
`Step` that safety-checks the terms of node and incoming message to prevent
stale log entries:

- **MsgHup** is used for election. If a node is a follower or candidate, the
	`Tick` function in `raft` struct calls `tickElection`. If a follower or
	candidate has not received any heartbeat before the election timeout, it
	passes `MsgHup` to its Step method and becomes (or remains) a candidate to
	start a new election.

- **MsgBeat** is an internal type that signals the leader to send a heartbeat of
	the `MsgHeartbeat` type. If a node is a leader, the `Tick` function in
	the `raft` struct calls `tickHeartbeat`, and triggers the leader to
	send periodic `MsgHeartbeat` messages to its followers.

- **MsgProp** proposes to append data to its log entries. This is a special
	type to redirect proposals to leader. Therefore, send method overwrites
	`pb::Message`'s term with its HardState's term to avoid attaching its
	local term to `MsgProp`. When `MsgProp` is passed to the leader's `Step`
	method, the leader first calls the `appendRawEntries` method to append entries
	to its log, and then calls `bcastAppend` method to send those entries to
	its peers. When passed to candidate or follower, `MsgProp` is dropped.

-  **MsgApp** contains log entries to replicate. A leader calls `bcastAppend`,
	which calls `sendAppend`, which sends soon-to-be-replicated logs in `MsgApp`
	type. When `MsgApp` is passed to candidate's `Step` method, candidate reverts
	back to follower, because it indicates that there is a valid leader sending
	`MsgApp` messages. Candidate and follower respond to this message in
	`MsgAppResp` type.

- **MsgAppResp** is response to log replication request(`MsgApp`). When
	`MsgApp` is passed to candidate or follower's Step method, it responds by
	calling `handleAppendEntries` method, which sends `MsgAppResp` to raft
	mailbox. When leader receives the`MsgAppResp`, it updates its state by calling
	`handleMsgAppResp` method. When the response were not rejected, the leader
	advances its `matchIndex`, and advances `commitIndex` to the largest `matchIndex`
	on majority. The leader will retry with lower `nextIndex` when the follower
	responds with denial.

- **MsgVote** requests votes for election. When a node is a follower or
	candidate and `MsgHup` is passed to its Step method, then the node calls
	`campaign` method to campaign itself to become a leader. Once `campaign`
	method is called, the node becomes candidate and sends `MsgVote` to peers
	in cluster to request votes. When passed to leader or candidate's Step
	method and the message's Term is lower than leader's or candidate's,
	`MsgVote` will be rejected (`MsgVoteResp` is returned with Reject true).
	If leader or candidate receives `MsgVote` with higher term, it will revert
	back to follower. When `MsgVote` is passed to follower, it votes for the
	sender only when sender's last term is greater than MsgVote's term or
	sender's last term is equal to MsgVote's term but sender's last committed
	index is greater than or equal to follower's.

- **MsgVoteResp** contains responses from voting request. When `MsgVoteResp` is
	passed to candidate, the candidate calculates how many votes it has won. If
	it's more than majority (quorum), it becomes leader and calls `bcastAppend`.
	If candidate receives majority of votes of denials, it reverts back to
	follower.

- **MsgPreVote** and **MsgPreVoteResp** are used in an optional two-phase election
	protocol. When `Config::preVote` is true, a pre-election is carried out first
	(using the same rules as a regular election), and no node increases its term
	number unless the pre-election indicates that the campaigning node would win.
	This minimizes disruption when a partitioned node rejoins the cluster.

- **MsgSnap** requests to install a snapshot message. When a node has just
	become a leader or the leader receives `MsgProp` message, it calls
	`bcastAppend` method, which then calls `sendAppend` method to each
	follower. In `sendAppend`, if a leader fails to get term or entries,
	the leader requests snapshot by sending `MsgSnap` type message.

- **MsgSnapStatus** tells the result of snapshot install message. When a
	follower rejected `MsgSnap`, it indicates the snapshot request with
	`MsgSnap` had failed from network issues which causes the network layer
	to fail to send out snapshots to its followers. Then leader considers
	follower's progress as probe. When `MsgSnap` were not rejected, it
	indicates that the snapshot succeeded and the leader sets follower's
	progress to probe and resumes its log replication.

- **MsgHeartbeat** sends heartbeat from leader. When `MsgHeartbeat` is passed
	to candidate and message's term is higher than candidate's, the candidate
	reverts back to follower and updates its committed index from the one in
	this heartbeat. And it sends the message to its mailbox. When
	`MsgHeartbeat` is passed to follower's Step method and message's term is
	higher than follower's, the follower updates its leaderID with the ID
	from the message.

- **MsgHeartbeatResp** is a response to `MsgHeartbeat`. When `MsgHeartbeatResp`
	is passed to leader's Step method, the leader knows which follower
	responded. And only when the leader's last log index is greater than
	follower's Match index, the leader runs `sendAppend` method.

- **MsgUnreachable** tells that request(message) wasn't delivered. When
	`MsgUnreachable` is passed to leader's Step method, the leader discovers
	that the follower that sent this `MsgUnreachable` is not reachable, often
	indicating `MsgApp` is lost. When follower's progress state is replicate,
	the leader sets it back to probe.
