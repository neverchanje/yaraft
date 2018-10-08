// Copyright 2017 The Abseil Authors.
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
//

#include <cstddef>
#include <stdexcept>

#pragma once

namespace yaraft {

namespace span_internal {

// A constexpr min function
constexpr size_t Min(size_t a, size_t b) noexcept { return a < b ? a : b; }

}  // namespace span_internal

// A constant view over the underlying array.
template <typename T>
class const_span {
 public:
  using value_type = typename std::remove_cv<T>::type;
  using const_pointer = const T *;
  using const_reference = const T &;
  using const_iterator = const_pointer;
  using size_type = size_t;
  using difference_type = ptrdiff_t;

  static const size_type npos = ~(size_type(0));

  constexpr const_span() noexcept : const_span(nullptr, 0) {}

  constexpr const_span(const_pointer array, size_type length) noexcept
      : ptr_(array), len_(length) {}

  // Implicit conversion constructors
  template <size_t N>
  constexpr const_span(T (&a)[N]) noexcept  // NOLINT(runtime/explicit)
      : const_span(a, N) {}

  template <class Container>
  constexpr const_span(const Container &cont)  // NOLINT(explicit)
      : const_span(cont.data(), cont.size()) {}

  constexpr const_pointer data() const noexcept { return ptr_; }

  constexpr size_type size() const noexcept { return len_; }

  constexpr size_type length() const noexcept { return size(); }

  constexpr bool empty() const noexcept { return size() == 0; }

  constexpr const_iterator begin() const noexcept { return ptr_; }

  constexpr const_iterator cbegin() const noexcept { return begin(); }

  const_iterator end() const noexcept { return ptr_ + len_; }

  const_iterator cend() const noexcept { return end(); }

  constexpr const_reference operator[](size_type i) const noexcept {
    // MSVC 2015 accepts this as constexpr, but not ptr_[i]
    return *(data() + i);
  }

  constexpr const_span subspan(size_type pos = 0, size_type len = npos) const {
    return (pos <= len_) ? const_span(ptr_ + pos, span_internal::Min(len_ - pos, len))
                         : (throw std::out_of_range("pos > size()"), const_span());
  }

  const_span Slice(size_type begin, size_type end) const {
    if (begin > end) {
      throw std::invalid_argument("begin > end");
    }
    return subspan(begin, end - begin);
  }

  const_span SliceFrom(size_type begin) const { return subspan(begin); }

  const_span SliceTo(size_type end) const { return Slice(0, end); }

 private:
  const_pointer ptr_;
  size_type len_;
};

}  // namespace yaraft
