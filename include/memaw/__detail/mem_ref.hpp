#pragma once
#include <atomic>
#include <atomic128/atomic128_ref.hpp>
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

using namespace atomic128;

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

template <typename T>
struct mem_ref<T, thread_safe> {
public:
  T load(const mo_t mo) const noexcept {
    return std::atomic_ref(ref).load(mo);
  }

  void store(const T& val, const mo_t mo) const noexcept {
    std::atomic_ref(ref).store(val, mo);
  }

  template <std::same_as<mo_t>... MOs>
  bool compare_exchange_strong(T& old_val, const T& new_val,
                               const MOs... mos) const noexcept {
    return
      std::atomic_ref(ref).compare_exchange_strong(old_val, new_val, mos...);
  }

  template <std::same_as<mo_t>... MOs>
  bool compare_exchange_weak(T& old_val, const T& new_val,
                             const MOs... mos) const noexcept {
    return
      std::atomic_ref(ref).compare_exchange_weak(old_val, new_val, mos...);
  }

  T& ref;
};

template <atomic128_referenceable T>
struct mem_ref<T, thread_safe> {
public:
  template <std::same_as<mo_t>... MOs>
  bool compare_exchange_strong(T& old_val, const T& new_val,
                               const MOs... mos) const noexcept {
    return
      atomic128_ref(ref).compare_exchange_strong(old_val, new_val, mos...);
  }

  template <std::same_as<mo_t>... MOs>
  bool compare_exchange_weak(T& old_val, const T& new_val,
                             const MOs... mos) const noexcept {
    return
      atomic128_ref(ref).compare_exchange_weak(old_val, new_val, mos...);
  }

  T& ref;
};

/**
 * @brief A factory function for mem_ref that allows to avoid
 *        specifying the reference type explicitly
 **/
template <thread_safety_t _ts, typename T>
constexpr auto make_mem_ref(T& ref) noexcept {
  return mem_ref<T, _ts>(ref);
}

} // namespace memaw::__detail
