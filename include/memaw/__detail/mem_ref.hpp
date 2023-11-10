#pragma once
#include <atomic>
#include <concepts>

/**
 * @file
 * A reference wrapper that allows to switch between atomic and non
 * thread safe implementations with a template argument
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

using mo_t = std::memory_order;

enum thread_safety_t {
  thread_unsafe,
  thread_safe
};

/**
 * @brief A wrapper providing read/write methods to a reference of the
 *        given type with their implementation depending on the chosen
 *        thread safety policy
 **/
template <typename, thread_safety_t>
struct mem_ref;

template <typename T>
struct mem_ref<T, thread_unsafe> {
public:
  T load(const mo_t) const noexcept {
    return ref;
  }

  void store(const T& value, const mo_t) const noexcept {
    ref = value;
  }

  bool compare_exchange_strong(T& old_val, const T& new_val, const mo_t,
                               const mo_t = mo_t::relaxed) const noexcept {
    if (ref != old_val) {
      old_val = ref;
      return false;
    }

    ref = new_val;
    return true;
  }

  bool compare_exchange_weak(T& old_val, const T& new_val, const mo_t,
                             const mo_t = mo_t::relaxed) const noexcept {
    return compare_exchange_strong(old_val, new_val, mo_t::relaxed);
  }

  T& ref;
};

} // namespace memaw::__detail
