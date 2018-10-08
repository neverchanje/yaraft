namespace cpp yaraft.thrift

enum EntryType {
    EntryNormal;
    EntryConfChange;
}

struct Entry {
    2:optional i64       Term ; // must be 64-bit aligned for atomic operations
    3:optional i64       Index; // must be 64-bit aligned for atomic operations
    1:optional EntryType Type ;
    4:optional string    Data ;
}

struct ConfState {
    1:list<i64> nodes   ;
    2:list<i64> learners;
}

struct SnapshotMetadata {
    1:optional ConfState conf_state;
    2:optional i64       index     ;
    3:optional i64       term      ;
}

struct Snapshot {
    1:optional string           data    ;
    2:optional SnapshotMetadata metadata;
}

enum MessageType {
  MsgHup           ;
  MsgBeat          ;
  MsgProp          ;
  MsgApp           ;
  MsgAppResp       ;
  MsgVote          ;
  MsgVoteResp      ;
  MsgSnap          ;
  MsgHeartbeat     ;
  MsgHeartbeatResp ;
  MsgUnreachable   ;
  MsgSnapStatus    ;
  MsgCheckQuorum   ;
  MsgTransferLeader;
  MsgTimeoutNow    ;
  MsgReadIndex     ;
  MsgReadIndexResp ;
  MsgPreVote       ;
  MsgPreVoteResp   ;
}

struct Message {
    1: optional MessageType type      ;
    2: optional i64         to        ;
    3: optional i64         nfrom     ; // `from` is reserved for thrift
    4: optional i64         term      ;
    5: optional i64         logTerm   ;
    6: optional i64         index     ;
    7:          list<Entry> entries   ;
    8: optional i64         commit    ;
    9: optional Snapshot    snapshot  ;
    10:optional bool        reject    ;
    11:optional i64         rejectHint;
    12:optional string      context   ;
}

struct HardState {
    1:optional i64 term  ;
    2:optional i64 vote  ;
    3:optional i64 commit;
}

enum ConfChangeType {
    ConfChangeAddNode       ;
    ConfChangeRemoveNode    ;
    ConfChangeUpdateNode    ;
    ConfChangeAddLearnerNode;
}

struct ConfChange {
    1:optional i64            ID     ;
    2:optional ConfChangeType Type   ;
    3:optional i64            NodeID ;
    4:optional string         Context;
}
