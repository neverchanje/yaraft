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

#include "util.h"

namespace yaraft {

/*extern*/ idl::EntrySpan limitSize(uint64_t maxSize, idl::EntrySpan ents) {
  if (ents.empty()) {
    return {};
  }

  size_t size = ents[0].ByteSize();
  size_t limit = 1;
  for (; limit < ents.size(); limit++) {
    size += ents[limit].ByteSize();
    if (size > maxSize) {
      break;
    }
  }
  return ents.SliceTo(limit);  // return ents[:limit]
}

}  // namespace yaraft
