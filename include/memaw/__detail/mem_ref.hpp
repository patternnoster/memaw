#pragma once
#include <atomic>
#include <atomic128/atomic128_ref.hpp>
#include <concepts>
#include <utility>

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

  T exchange(T value, const mo_t) const noexcept {
    std::swap(value, ref);
    return value;
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

#ifdef __cpp_lib_atomic_ref
/* Use std::atomic_ref for thread_safe calls by default if it's
 * available */
template <typename T>
struct mem_ref<T, thread_safe> {
public:
  T load(const mo_t mo) const noexcept {
    return std::atomic_ref(ref).load(mo);
  }

  void store(const T& val, const mo_t mo) const noexcept {
    std::atomic_ref(ref).store(val, mo);
  }

  T exchange(const T& val, const mo_t mo) const noexcept {
    return std::atomic_ref(ref).exchange(val, mo);
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
#elif defined(__GCC_ATOMIC_POINTER_LOCK_FREE)
/* Alternatively use the __atomic_* built-ins for systems that do not
 * yet support std::atomic_ref (e.g., libc++ as of now, in the year
 * 20-freaking-23) */
template <typename T>
struct mem_ref<T, thread_safe> {
public:
  /* We're not going to do runtime conversions since this is a
   * reasonable thing to require, it's true for our target compilers
   * and we're going to get rid of this code as soon as libc++
   * implements std::atomic_ref anyway */
  static_assert(unsigned(mo_t::relaxed) == __ATOMIC_RELAXED);
  static_assert(unsigned(mo_t::consume) == __ATOMIC_CONSUME);
  static_assert(unsigned(mo_t::acquire) == __ATOMIC_ACQUIRE);
  static_assert(unsigned(mo_t::release) == __ATOMIC_RELEASE);
  static_assert(unsigned(mo_t::acq_rel) == __ATOMIC_ACQ_REL);
  static_assert(unsigned(mo_t::seq_cst) == __ATOMIC_SEQ_CST);

  T load(const mo_t mo) const noexcept {
    return __atomic_load_n(&ref, unsigned(mo));
  }

  void store(const T& val, const mo_t mo) const noexcept {
    __atomic_store_n(&ref, val, unsigned(mo));
  }

  T exchange(const T& val, const mo_t mo) const noexcept {
    return __atomic_exchange_n(&ref, val, unsigned(mo));
  }

  template <std::same_as<mo_t>... MOs>
  bool compare_exchange_strong(T& old_val, const T& new_val,
                               const MOs... mos) const noexcept {
    return __atomic_compare_exchange_n(&ref, &old_val, new_val, false,
                                       unsigned(mos)...);
  }

  template <std::same_as<mo_t>... MOs>
  bool compare_exchange_weak(T& old_val, const T& new_val,
                             const MOs... mos) const noexcept {
    return __atomic_compare_exchange_n(&ref, &old_val, new_val, true,
                                       unsigned(mos)...);
  }

  T& ref;
};
#else
#  error Either std::atomic_ref or __atomic_* built-ins must be available
#endif

template <atomic128_referenceable T>
struct mem_ref<T, thread_safe> {
public:
  T exchange(const T& val, const mo_t mo) const noexcept {
    return atomic128_ref(ref).exchange(val, mo);
  }

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
