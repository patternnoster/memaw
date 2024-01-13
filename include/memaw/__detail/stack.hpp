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

template <stackable T, thread_safety_t _ts>
void stack<T, _ts>::push(T* const ptr) noexcept {
  // Since we don't really access the head pointer, relaxed reads are
  // perfectly fine here (and in case of CAS failure)
  head_t old_head{
    .ptr = make_mem_ref<_ts>(head_.ptr).load(mo_t::relaxed),
    .aba_counter = make_mem_ref<_ts>(head_.aba_counter).load(mo_t::relaxed)
  };

  head_t new_head;
  new_head.ptr = ptr;

  const auto next_ref = make_mem_ref<_ts>(ptr->next);
  const auto head_ref = make_mem_ref<_ts>(head_);
  do {
    next_ref.store(old_head.ptr, mo_t::relaxed);
    new_head.aba_counter = old_head.aba_counter + 1;
  }
  while (!head_ref.compare_exchange_weak(old_head, new_head,
                                         mo_t::release,
                                         mo_t::relaxed));
}

template <stackable T, thread_safety_t _ts>
T* stack<T, _ts>::pop() noexcept {
  // We need to always acquire the head since we're planning to read
  // from the corresponding pointer
  head_t old_head{
    .ptr = make_mem_ref<_ts>(head_.ptr).load(mo_t::acquire),
    .aba_counter = make_mem_ref<_ts>(head_.aba_counter).load(mo_t::relaxed)
  };

  head_t new_head;
  const auto head_ref = make_mem_ref<_ts>(head_);
  do {
    if (!old_head.ptr) return nullptr;  // Empty stack

    new_head = {
      .ptr = make_mem_ref<_ts>(old_head.ptr->next).load(mo_t::relaxed),
      .aba_counter = old_head.aba_counter + 1
    };
  }
  while (!head_ref.compare_exchange_weak(old_head, new_head,
                                         mo_t::relaxed,
                 /* C++20 allows this: */mo_t::acquire));

  return old_head.ptr;
}

} // namespace memaw::__detail
