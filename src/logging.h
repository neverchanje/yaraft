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

#include "logger.h"

#include <fmt/format.h>
#include <silly/likely.h>

#define LOG(level, str) raftLogger->Log(level, __LINE__, __FILE__, str)

#define LOG_ASSERT(expr) (expr) ? void(0) : LOG(FATAL, "Assertion failed: " #expr)
#define LOG_ASSERT_S(expr, msg) \
  if (UNLIKELY(!(expr))) {      \
    DLOG(FATAL, msg);           \
  }

#ifndef NDEBUG
#define DLOG(level, str) LOG(level, str)
#define DLOG_ASSERT(expr) LOG_ASSERT(expr)
#define DLOG_ASSERT_S(expr, msg) LOG_ASSERT_S(expr, msg)
#else
#define DLOG(level, str) true ? void(0) : LOG(level, str)
#define DLOG_ASSERT(expr) true ? void(0) : LOG_ASSERT(expr)
#define DLOG_ASSERT_S(expr, msg) LOG_ASSERT_S(expr, msg)
#endif

#define FMT_LOG(level, formatStr, args...) LOG(level, fmt::format(formatStr, ##args))

#define FMT_SLOG(level, formatStr, args...) LOG(level, fmt::sprintf(formatStr, ##args))

#define D_FMT_LOG(level, formatStr, args...) DLOG(level, fmt::format(formatStr, ##args))

#define D_FMT_SLOG(level, formatStr, args...) DLOG(level, fmt::sprintf(formatStr, ##args))

/// @brief Emit a fatal error if @c to_call returns a bad status.
#define FATAL_NOT_OK(to_call, fatal_prefix)                    \
  do {                                                         \
    const auto& _s = (to_call);                                \
    if (UNLIKELY(!_s.IsOK())) {                                \
      FMT_LOG(FATAL, "{}: {}", (fatal_prefix), _s.ToString()); \
    }                                                          \
  } while (0);

namespace yaraft {

extern std::unique_ptr<Logger> raftLogger;

}  // namespace yaraft