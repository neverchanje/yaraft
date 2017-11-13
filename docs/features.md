# Features in yaraft

yaraft doesn't support the [full list of features](https://github.com/coreos/etcd/tree/master/raft#features) 
included in etcd/raft.

- [x] Leader election
- [x] Log replication
- [x] Log Compaction / InstallSnapshot
- [x] Membership changes
- [ ] Leader Transfer
- [x] Linearizable read-only queries
- [x] Pipelining
- [ ] Flow Control
- [x] Batching Raft messages
- [x] Batching log entries
- [ ] Proposal forwarding from followers to leader
- [ ] CheckQuorum
- [x] PreVote

Leader election, log replication, and log compaction are the most basic functions that the Raft protocol provides. 
Read [Raft paper](https://raft.github.io/raft.pdf) or [Raft thesis](https://ramcloud.stanford.edu/~ongaro/thesis.pdf) 
for more knowledge about the algorithm.

Membership changes means dynamically adding or removing nodes from cluster. It's two methods stated in
Raft thesis chapter 4 on this problem. The method 1 restricts that servers can only be added or removed 
one by one, and the method 2 allows arbitrary membership changes but is more complicated.
We only implemented the single-server approach.

PreVote is an optimization on the voting process stated in Raft thesis 9.6.
