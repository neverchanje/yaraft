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

#pragma once

#include "idl_wrapper.h"

#include <yaraft/span.h>
#include <cstddef>
#include <vector>

namespace yaraft {

// GoSlice is a combination of std::vector and span, provides the
// functionality resembling golang slice.
template <typename T>
class GoSlice {
 public:
  using value_type = typename std::remove_cv<T>::type;
  using size_type = size_t;
  using const_pointer = const T *;
  using const_reference = const T &;
  using const_iterator = const_pointer;
  using pointer = T *;
  using reference = T &;
  using iterator = pointer;

  constexpr GoSlice() = default;

  GoSlice(std::initializer_list<value_type> list)
      : data_(list), view_(data_) {
  }
  GoSlice &operator=(std::initializer_list<value_type> list) {
    data_ = list;
    view_ = data_;
    return *this;
  }

  explicit GoSlice(const_span<T> ents) {
    *this = ents;
  }
  GoSlice &operator=(const_span<T> ents) {
    data_.clear();
    view_ = data_;
    Append(ents);
    return *this;
  }

  // Equivalent with golang s := make([]type, len, cap)
  static GoSlice<T> make(size_type len, size_type cap) {
    GoSlice ret;
    ret.data_.reserve(cap);
    ret.data_.resize(len);
    ret.view_ = const_span<T>(ret.data_);
    return ret;
  }
  // Equivalent with golang s := make([]type, len)
  static GoSlice<T> make(size_type len) {
    GoSlice ret;
    ret.data_.resize(len);
    ret.view_ = const_span<T>(ret.data_);
    return ret;
  }

  constexpr size_type capacity() const noexcept { return data_.capacity(); }

  constexpr pointer data() const noexcept {
    return const_cast<pointer>(view_.data());
  }

  constexpr size_type size() const noexcept { return view_.size(); }

  constexpr bool empty() const noexcept { return view_.empty(); }

  reference operator[](size_type i) noexcept {
    return const_cast<reference>(view_[i]);
  }
  const_reference operator[](size_type i) const noexcept {
    return view_[i];
  }

  constexpr iterator begin() const noexcept {
    return const_cast<iterator>(view_.begin());
  }

  constexpr const_iterator cbegin() const noexcept { return view_.begin(); }

  constexpr iterator end() const noexcept {
    return const_cast<iterator>(view_.end());
  }

  constexpr const_iterator cend() const noexcept { return end(); }

  void push_back(T e) {
    uint64_t off = data_.size() - view_.size();
    data_.push_back(std::move(e));
    view_ = const_span<T>(data_.data() + off, view_.size() + 1);
  }

  constexpr const_span<T> View() const noexcept { return view_; }

  // golang equivalent: entries := append(entries, ents...)
  void Append(const_span<T> ents) {
    if (ents.empty()) {
      return;
    }
    size_type offset = data_.size() - view_.size();
    data_.reserve(ents.size() + data_.size());
    for (T e : ents) {
      data_.push_back(std::move(e));
    }
    view_ = const_span<T>(data_.data() + offset, view_.size() + ents.size());
  }

  // golang equivalent: ents := ents[begin:end]
  void Slice(size_type begin, size_type end) {
    size_type off = data_.size() - view_.size();
    view_ = view_.Slice(begin, end);

    // truncate [end ... data_.end()]
    data_.resize(view_.size() + off);
  }

  // golang equivalent: ents := ents[begin:]
  void SliceFrom(size_type begin) {
    view_ = view_.SliceFrom(begin);
  }

  // golang equivalent: ents := ents[:end]
  void SliceTo(size_type end) {
    Slice(0, end);
  }

 private:
  std::vector<T> data_;
  const_span<T> view_;
};

}  // namespace yaraft
