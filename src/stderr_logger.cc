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

#include <chrono>

#include "port.h"
#include "stderr_logger.h"

#include <fmt/format.h>
#include <fmt/time.h>
#include <syscall.h>
#include <unistd.h>

namespace yaraft {

inline char LogLevelToChar(LogLevel level) {
  char levelChar;
  switch (level) {
    case INFO:
      levelChar = 'I';
      break;
    case ERROR:
      levelChar = 'E';
      break;
    case WARNING:
      levelChar = 'W';
      break;
    case FATAL:
      levelChar = 'F';
      break;
    default:
      fprintf(stderr, "unexpected log level %d", level);
      assert(false);
  }
  return levelChar;
}

inline const char* const_basename(const char* filepath) {
  const char* base = strrchr(filepath, '/');
#ifdef OS_WINDOWS  // Look for either path separator in Windows
  if (!base)
    base = strrchr(filepath, '\\');
#endif
  return base ? (base + 1) : filepath;
}

inline pid_t GetTID() {
// On Linux and MacOSX, we try to use gettid().
#if defined OS_LINUX || defined OS_MACOSX
#ifndef __NR_gettid
#ifdef OS_MACOSX
#define __NR_gettid SYS_gettid
#elif !defined __i386__
#error "Must define __NR_gettid for non-x86 platforms"
#else
#define __NR_gettid 224
#endif
#endif
  static bool lacks_gettid = false;
  if (!lacks_gettid) {
    pid_t tid = syscall(__NR_gettid);
    if (tid != -1) {
      return tid;
    }
    // Technically, this variable has to be volatile, but there is a small
    // performance penalty in accessing volatile variables and there should
    // not be any serious adverse effect if a thread does not immediately see
    // the value change to "true".
    lacks_gettid = true;
  }
#endif  // OS_LINUX || OS_MACOSX

// If gettid() could not be used, we use one of the following.
#if defined OS_LINUX
  return getpid();  // Linux:  getpid returns thread ID when gettid is absent
#elif defined OS_WINDOWS && !defined OS_CYGWIN
  return GetCurrentThreadId();
#else
  // If none of the techniques above worked, we use pthread_self().
  return (pid_t)(uintptr_t)pthread_self();
#endif
}

void StderrLogger::Log(LogLevel level, int line, const char* file, const Slice& log) {
  // we use the log format described in google/glog:
  //
  // LOG LINE PREFIX FORMAT
  //
  // Log lines have this form:
  //
  //     Lmmdd hh:mm:ss.uuuuuu threadid file:line] msg...
  //
  // where the fields are defined as follows:
  //
  //   L                A single character, representing the log level
  //                    (eg 'I' for INFO)
  //   mm               The month (zero padded; ie May is '05')
  //   dd               The day (zero padded)
  //   hh:mm:ss.uuuuuu  Time in hours, minutes and fractional seconds
  //   threadid         The space-padded thread ID as returned by GetTID()
  //                    (this matches the PID on Linux)
  //   file             The file name
  //   line             The line number
  //   msg              The user-supplied message
  //
  // Example:
  //
  //   I1103 11:57:31.739339 24395 google.cc:2341] Command line: ./some_prog
  //   I1103 11:57:31.739403 24395 google.cc:2342] Process id 24395
  //
  // NOTE: although the microseconds are useful for comparing events on
  // a single machine, clocks on different machines may not be well
  // synchronized.  Hence, use caution when comparing the low bits of
  // timestamps from different machines.

  auto sysclock_now = std::chrono::system_clock::now();
  auto now = std::chrono::system_clock::to_time_t(sysclock_now);

  int64_t usecs =
      std::chrono::duration_cast<std::chrono::microseconds>(sysclock_now.time_since_epoch())
          .count() -
      std::chrono::duration_cast<std::chrono::seconds>(sysclock_now.time_since_epoch()).count() *
          1000000;

  const char* basename = const_basename(file);

  fmt::fprintf(stderr, "%c%s.%ld %5u %s:%d] %s\n", LogLevelToChar(level),
               fmt::format("{:%m%d %H:%M:%S}", *localtime(&now)), usecs, GetTID(), basename, line,
               log.RawData());

  if (level == FATAL) {
    abort();
  }
}

}  // namespace yaraft