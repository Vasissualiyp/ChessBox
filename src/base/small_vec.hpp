// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <span>

namespace cb {

/// Fixed-capacity vector with no heap allocation, for the hot paths where the
/// bound is known statically (coordinate axes, direction supports, atom
/// magnitudes). Overflow is a programming error: `push` asserts, `tryPush`
/// reports. Never silently drops elements - see AGENTS.md.
template <class T, std::size_t N>
class SmallVec {
 public:
  using value_type = T;

  constexpr SmallVec() = default;
  constexpr SmallVec(std::initializer_list<T> init) {
    for (const T& v : init) push(v);
  }

  constexpr void push(const T& v) {
    assert(size_ < N && "SmallVec overflow");
    data_[size_++] = v;
  }
  [[nodiscard]] constexpr bool tryPush(const T& v) {
    if (size_ >= N) return false;
    data_[size_++] = v;
    return true;
  }
  constexpr void pop() {
    assert(size_ > 0);
    --size_;
  }
  constexpr void clear() noexcept { size_ = 0; }
  constexpr void resize(std::size_t n) {
    assert(n <= N);
    size_ = n;
  }

  [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }

  constexpr T& operator[](std::size_t i) {
    assert(i < size_);
    return data_[i];
  }
  constexpr const T& operator[](std::size_t i) const {
    assert(i < size_);
    return data_[i];
  }
  constexpr T& back() {
    assert(size_ > 0);
    return data_[size_ - 1];
  }

  constexpr T* begin() noexcept { return data_.data(); }
  constexpr T* end() noexcept { return data_.data() + size_; }
  constexpr const T* begin() const noexcept { return data_.data(); }
  constexpr const T* end() const noexcept { return data_.data() + size_; }

  [[nodiscard]] constexpr std::span<const T> span() const noexcept {
    return {data_.data(), size_};
  }

  friend constexpr bool operator==(const SmallVec& a, const SmallVec& b) {
    if (a.size_ != b.size_) return false;
    for (std::size_t i = 0; i < a.size_; ++i) {
      if (!(a.data_[i] == b.data_[i])) return false;
    }
    return true;
  }

 private:
  std::array<T, N> data_{};
  std::size_t size_{0};
};

}  // namespace cb
