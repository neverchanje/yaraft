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

#include <fmt/format.h>
#include <glog/logging.h>

#define FMT_LOG(level, formatStr, args...) LOG(level) << fmt::format(formatStr, ##args)

#define FMT_SLOG(level, formatStr, args...) LOG(level) << fmt::sprintf(formatStr, ##args)

#define D_FMT_LOG(level, formatStr, args...) DLOG(level) << fmt::format(formatStr, ##args)

#define D_FMT_SLOG(level, formatStr, args...) DLOG(level) << fmt::sprintf(formatStr, ##args)

/// @brief Emit a fatal error if @c to_call returns a bad status.
#define FATAL_NOT_OK(to_call, fatal_prefix)                  \
  do {                                                       \
    const auto& _s = (to_call);                              \
    if (UNLIKELY(!_s.IsOK())) {                              \
      LOG(FATAL) << (fatal_prefix) << ": " << _s.ToString(); \
    }                                                        \
  } while (0)
