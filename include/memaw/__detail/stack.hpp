#pragma once
#include <concepts>

#include "base.hpp"
#include "mem_ref.hpp"

/**
 * @file
 * A lock-free stack implementation based on mem_ref
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

template <typename T>
concept stackable = requires(T item) {
  { item.next } -> std::same_as<T*&>;
};

/**
 * @brief A simple lock-free stack of pointers
 **/
template <stackable T, thread_safety_t _ts>
class stack {
public:
  stack() noexcept: head_{} {}

  stack(const stack&) = delete;
  stack& operator=(const stack&) = delete;
  stack& operator=(stack&&) = delete;

  stack(stack&& rhs): head_{ .ptr = rhs.reset(), .aba_counter = 0 } {}

  inline void push(T*) noexcept;
  inline T* pop() noexcept;

  /**
   * @brief Clears the stack and returns the old stack head
   **/
  T* reset() noexcept {
    const auto old_head = make_mem_ref<_ts>(head_).exchange({}, mo_t::acquire);
    return old_head.ptr;
  }

private:
  struct alignas(16) head_t {
    T* ptr;
    size_t aba_counter;

    bool operator==(const head_t&) const noexcept = default;
  };
  head_t head_;
};

} // namespace memaw::__detail
