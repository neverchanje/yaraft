// Copyright 17 The etcd Authors
// Copyright 17 Wu Tao
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

#include <yaraft/memory_storage.h>

namespace yaraft {

// ErrCompacted is returned by Storage.Entries/Compact when a requested
// index is unavailable because it predates the last snapshot.
const error_s ErrCompacted = error_s::make(
    ERR_RAFT_STORAGE, "requested index is unavailable due to compaction");

// ErrSnapOutOfDate is returned by Storage.CreateSnapshot when a requested
// index is older than the existing snapshot.
const error_s ErrSnapOutOfDate = error_s::make(
    ERR_RAFT_STORAGE, "requested index is older than the existing snapshot");

// ErrUnavailable is returned by Storage interface when the requested log entries
// are unavailable.
const error_s ErrUnavailable =
    error_s::make(ERR_RAFT_STORAGE, "requested entry at index is unavailable");

// ErrSnapshotTemporarilyUnavailable is returned by the Storage interface when the required
// snapshot is temporarily unavailable.
const error_s ErrSnapshotTemporarilyUnavailable =
    error_s::make(ERR_RAFT_STORAGE, "snapshot is temporarily unavailable");

}  // namespace yaraft
