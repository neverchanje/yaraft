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

#include <memory>

#include <fmt/printf.h>
#include <yaraft/string_view.h>

namespace yaraft {

enum LogLevel : unsigned char {
  DEBUG = 1,
  INFO,
  WARN,
  ERROR,
  PANIC,

  NUM_LOG_LEVELS
};

class Logger {
 public:
  virtual ~Logger() = default;

  virtual void Log(LogLevel level, int line, const char *file,
                   string_view log) = 0;
};

extern std::unique_ptr<Logger> raftLogger;

#define RAFT_LOG(level, formatStr, args...) \
  raftLogger->Log(level, __LINE__, __FILE__, fmt::sprintf(formatStr, ##args))

// An equivalent for golang panic()
#define RAFT_PANIC(err) \
  raftLogger->Log(PANIC, __LINE__, __FILE__, fmt::format("{}", err))

}  // namespace yaraft
