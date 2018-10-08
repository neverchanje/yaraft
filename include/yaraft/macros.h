// Copyright 2017 The Abseil Authors.
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

// These macros are copied from google/abseil-cpp

// RAFT_HAVE_BUILTIN()
//
// Checks whether the compiler supports a Clang Feature Checking Macro, and if
// so, checks whether it supports the provided builtin function "x" where x
// is one of the functions noted in
// https://clang.llvm.org/docs/LanguageExtensions.html
//
// Note: Use this macro to avoid an extra level of #ifdef __has_builtin check.
// http://releases.llvm.org/3.3/tools/clang/docs/LanguageExtensions.html
#ifdef __has_builtin
#define RAFT_HAVE_BUILTIN(x) __has_builtin(x)
#else
#define RAFT_HAVE_BUILTIN(x) 0
#endif

// RAFT_PREDICT_TRUE, RAFT_PREDICT_FALSE
//
// Enables the compiler to prioritize compilation using static analysis for
// likely paths within a boolean branch.
//
// Example:
//
//   if (RAFT_PREDICT_TRUE(expression)) {
//     return result;                        // Faster if more likely
//   } else {
//     return 0;
//   }
//
// Compilers can use the information that a certain branch is not likely to be
// taken (for instance, a CHECK failure) to optimize for the common case in
// the absence of better information (ie. compiling gcc with `-fprofile-arcs`).
#if RAFT_HAVE_BUILTIN(__builtin_expect) || \
    (defined(__GNUC__) && !defined(__clang__))
#define RAFT_PREDICT_FALSE(x) (__builtin_expect(x, 0))
#define RAFT_PREDICT_TRUE(x) (__builtin_expect(!!(x), 1))
#else
#define RAFT_PREDICT_FALSE(x) (x)
#define RAFT_PREDICT_TRUE(x) (x)
#endif

// RAFT_ASSERT()
//
// In C++11, `assert` can't be used portably within constexpr functions.
// ABSL_ASSERT functions as a runtime assert but works in C++11 constexpr
// functions.  Example:
//
// constexpr double Divide(double a, double b) {
//   return ABSL_ASSERT(b != 0), a / b;
// }
//
// This macro is inspired by
// https://akrzemi1.wordpress.com/2017/05/18/asserts-in-constexpr-functions/
#if defined(NDEBUG)
#define RAFT_ASSERT(expr) (false ? (void)(expr) : (void)0)
#else
#define RAFT_ASSERT(expr)              \
  (RAFT_PREDICT_TRUE((expr)) ? (void)0 \
                             : [] { assert(#expr); }())  // NOLINT
#endif
