//
// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: string_view.h
// -----------------------------------------------------------------------------
//
// This file contains the definition of the `absl::string_view` class. A
// `string_view` points to a contiguous span of characters, often part or all of
// another `std::string`, double-quoted std::string literal, character array, or
// even another `string_view`.
//
// This `absl::string_view` abstraction is designed to be a drop-in
// replacement for the C++17 `std::string_view` abstraction.

#pragma once

#include <cassert>
#include <cstring>
#include <limits>
#include <string>

#include <yaraft/macros.h>

namespace yaraft {

class string_view {
 public:
  using traits_type = std::char_traits<char>;
  using value_type = char;
  using pointer = char *;
  using const_pointer = const char *;
  using reference = char &;
  using const_reference = const char &;
  using const_iterator = const char *;
  using iterator = const_iterator;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator = const_reverse_iterator;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;

  static constexpr size_type npos = static_cast<size_type>(-1);

  // Null `string_view` constructor
  constexpr string_view() noexcept : ptr_(nullptr), length_(0) {}

  // Implicit constructors

  template <typename Allocator>
  string_view(  // NOLINT(runtime/explicit)
      const std::basic_string<char, std::char_traits<char>, Allocator>
          &str) noexcept
      : ptr_(str.data()), length_(CheckLengthInternal(str.size())) {}

  // Implicit constructor of a `string_view` from nul-terminated `str`. When
  // accepting possibly null strings, use `absl::NullSafeStringView(str)`
  // instead (see below).
  constexpr string_view(const char *str)  // NOLINT(runtime/explicit)
      : ptr_(str), length_(CheckLengthInternal(StrLenInternal(str))) {}

  // Implicit constructor of a `string_view` from a `const char*` and length.
  constexpr string_view(const char *data, size_type len)
      : ptr_(data), length_(CheckLengthInternal(len)) {}

  // NOTE: Harmlessly omitted to work around gdb bug.
  //   constexpr string_view(const string_view&) noexcept = default;
  //   string_view& operator=(const string_view&) noexcept = default;

  // Iterators

  // string_view::begin()
  //
  // Returns an iterator pointing to the first character at the beginning of the
  // `string_view`, or `end()` if the `string_view` is empty.
  constexpr const_iterator begin() const noexcept { return ptr_; }

  // string_view::end()
  //
  // Returns an iterator pointing just beyond the last character at the end of
  // the `string_view`. This iterator acts as a placeholder; attempting to
  // access it results in undefined behavior.
  constexpr const_iterator end() const noexcept { return ptr_ + length_; }

  // string_view::cbegin()
  //
  // Returns a const iterator pointing to the first character at the beginning
  // of the `string_view`, or `end()` if the `string_view` is empty.
  constexpr const_iterator cbegin() const noexcept { return begin(); }

  // string_view::cend()
  //
  // Returns a const iterator pointing just beyond the last character at the end
  // of the `string_view`. This pointer acts as a placeholder; attempting to
  // access its element results in undefined behavior.
  constexpr const_iterator cend() const noexcept { return end(); }

  // Capacity Utilities

  // string_view::size()
  //
  // Returns the number of characters in the `string_view`.
  constexpr size_type size() const noexcept { return length_; }

  // string_view::length()
  //
  // Returns the number of characters in the `string_view`. Alias for `size()`.
  constexpr size_type length() const noexcept { return size(); }

  // string_view::empty()
  //
  // Checks if the `string_view` is empty (refers to no characters).
  constexpr bool empty() const noexcept { return length_ == 0; }

  // std::string:view::operator[]
  //
  // Returns the ith element of an `string_view` using the array operator.
  // Note that this operator does not perform any bounds checking.
  constexpr const_reference operator[](size_type i) const { return ptr_[i]; }

  // string_view::data()
  //
  // Returns a pointer to the underlying character array (which is of course
  // stored elsewhere). Note that `string_view::data()` may contain embedded nul
  // characters, but the returned buffer may or may not be nul-terminated;
  // therefore, do not pass `data()` to a routine that expects a nul-terminated
  // std::string.
  constexpr const_pointer data() const noexcept { return ptr_; }

 private:
  static constexpr size_type kMaxSize =
      std::numeric_limits<difference_type>::max();

  // check whether __builtin_strlen is provided by the compiler.
  // GCC doesn't have __has_builtin()
  // (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66970),
  // but has __builtin_strlen according to
  // https://gcc.gnu.org/onlinedocs/gcc-4.7.0/gcc/Other-Builtins.html.
#if RAFT_HAVE_BUILTIN(__builtin_strlen) || \
    (defined(__GNUC__) && !defined(__clang__))
  static constexpr size_type StrLenInternal(const char *str) {
    return str ? __builtin_strlen(str) : 0;
  }
#else
  static constexpr size_type StrLenInternal(const char *str) {
    return str ? strlen(str) : 0;
  }
#endif

  static constexpr size_type CheckLengthInternal(size_type len) {
    return RAFT_ASSERT(len <= kMaxSize), len;
  }

  const char *ptr_;
  size_type length_;
};

}  // namespace yaraft
