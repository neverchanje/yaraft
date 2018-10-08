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

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
#define OS_WINDOWS
#elif defined(__CYGWIN__) || defined(__CYGWIN32__)
#define OS_CYGWIN
#elif defined(linux) || defined(__linux) || defined(__linux__)
#ifndef OS_LINUX
#define OS_LINUX
#endif
#elif defined(macintosh) || defined(__APPLE__) || defined(__APPLE_CC__)
#define OS_MACOSX
#elif defined(__FreeBSD__)
#define OS_FREEBSD
#elif defined(__NetBSD__)
#define OS_NETBSD
#elif defined(__OpenBSD__)
#define OS_OPENBSD
#else
// more platforms?
#endif
