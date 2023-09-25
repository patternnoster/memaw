# Memaw

The library is still work in progress. See below for the list of features implemented so far.

## Members
### Concepts

| Name | Description |
|---|---|
| [**resource**](#resource) | the basic concept of a memory resource: can allocate and deallocate memory given size and alignment |
| [**bound_resource**](#bound_resource) | the concept of a resource that has a (constant) minimum allocation size limit |
| [**granular_resource**](#granular_resource) | the concepts of a bound resource that can only allocate sizes that are multiples of its minimum allocation |
| [**nothrow_resource**](#nothrow_resource) | the concept of a resource whose allocation, deallocation and equality testing methods don't throw exceptions |
| [**overaligning_resource**](#overaligning_resource) | the concept of a resource that has a (constant) guaranteed alignment greater than `alignof(std::max_align_t)` |
| [**sweeping_resource**](#sweeping_resource) | the concept of a resource that permits combining deallocations of adjacent regions into one call |
| [**thread_safe_resource**](#thread_safe_resource) | the concept of a resource whose methods can safely be called concurrently from different threads |

### Memory resources

| Name | Description |
|---|---|
| [**os_resource**](#os_resource) | memory resource that always allocates and frees memory via direct system calls to the OS |

### Helper concepts and types

| Name | Description |
|---|---|
| [**page_type**](#page_type) | the concept of a type that denotes the size of a system memory page and must be either one of the tags in [**page_types**](#page_types) or **pow2_t** for explicit size specification |
| [**page_types**](#page_types) | tags for the types of system memory pages available for allocation |

### Global variables

| Name | Description |
|---|---|
| [**enable_granular_resource**](#enable_granular_resource) | a specializable global constant that enables the [**granular_resource**](#granular_resource) concept |
| [**enable_sweeping_resource**](#enable_sweeping_resource) | a specializable global constant that enables the [**sweeping_resource**](#sweeping_resource) concept |
| [**enable_thread_safe_resource**](#enable_thread_safe_resource) | a specializable global constant that enables the [**thread_safe_resource**](#thread_safe_resource) concept |

## Details
### resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <typename R>
concept resource = std::equality_comparable<R>
  && requires(R res, void* ptr, size_t size, size_t alignment) {
  { res.allocate(size) } -> std::same_as<void*>;
  { res.allocate(size, alignment) } -> std::same_as<void*>;
  res.deallocate(ptr, size);
  res.deallocate(ptr, size, alignment);
};
```
The basic concept of a memory resource: can allocate and deallocate memory given size and alignment. All resources in the standard **&lt;memory_resource&gt;** and in this library model this concept.

The semantic requirements are straightforward:
* if `allocate(size)` or `allocate(size, alignment)` returns a non-null pointer, the byte range [ptr, ptr + size) must be accessible and not intersect with any range returned from a previous call, unless `deallocate()` corresponding (see below) to that call has been made;
* if alignment is a power of 2, `allocate(size, alignment)` must return a pointer aligned (at least) by alignment (meaning, `ptr & (alignment - 1) == 0`), whereas `allocate(size)` must return a pointer aligned by (at least) `alignof(std::max_align_t)`.

A call `r1.deallocate(ptr, size, [alignment])` corresponds to a call `r2.allocate(size2, [alignment2])`) if:
1. ptr was returned by that `allocate()` call;
2. `r1 == r2`;
3. `size == size2`;
4. `alignment == alignment2`, or not present in both calls.

We say that a `deallocate()` call is valid if and only if it has an `allocate()` call it corresponds to and no deallocation that corresponds to the same call has happened. A valid `deallocate()` call must succeed. Note that some resources might have less restrictive conditions of when `deallocate()` and `allocate()` correspond (e.g., the alignment parameter of `deallocate()` ignored).
For every `r.allocate()` that returned non-null pointer, a corresponding `deallocate()` call must be made before the destructor of r is called. All the memory allocated by r must be released by the time all destructors of instances that accepted those `deallocate()` calls return.

---

### bound_resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <typename R>
concept bound_resource = resource<R> && requires() {
  { R::min_size() } noexcept -> std::convertible_to<size_t>;
};
```
The concept of a resource that has a (constant) minimum allocation size limit. Such resources must define a static (but not necessarily constexpr) noexcept method `min_size()` that returns that minimum.

> [!NOTE]
> Semantic requirement: the return value of `R::min_size()` must never change (equality preservation) and be greater than 0.

---

### granular_resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <typename R>
concept granular_resource = bound_resource<R> && enable_granular_resource<R>;
```
The concepts of a bound resource that can only allocate sizes that are multiples of its `min_size()`.

---

### nothrow_resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <typename R>
concept nothrow_resource = resource<R>
  && __detail::nothrow_equality_comparable<R>
  && requires(R res, void* ptr, size_t size, size_t alignment) {
  { res.allocate(size) } noexcept;
  { res.allocate(size, alignment) } noexcept;
  { res.deallocate(ptr, size) } noexcept;
  { res.deallocate(ptr, size, alignment) } noexcept;
};
```
The concept of a resource whose allocation, deallocation and equality testing methods don't throw exceptions. Such resources return nullptr whenever allocation fails. All resources in this library model this concept.

---

### overaligning_resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <typename R>
concept overaligning_resource = resource<R> && requires() {
  { R::guaranteed_alignment() } noexcept -> std::same_as<pow2_t>;
};
```
The concept of a resource that has a (constant) guaranteed alignment greater than `alignof(std::max_align_t)`. Such resources must define a static (but not necessarily constexpr) method `guaranteed_alignment()` that returns that value.

Semantic requirements: the return value of `R::guaranteed_alignment()` must be a power of 2 greater than `alignof(std::max_align_t)` and must never change (equality preservation). Any pointers returned from an `allocate()` call to R must be aligned by (at least) that value.

---

### sweeping_resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <typename R>
concept sweeping_resource = resource<R> && enable_sweeping_resource<R>;
```
The concept of a resource that permits combining deallocations of adjacent regions into one call.

More formally, r is a sweeping resource if and only if for any two valid (see above) calls `r.deallocate(ptr1, size1, alignment1)` and `r.deallocate(ptr2, size2, alignment2)`, if `ptr2 == (byte*)ptr1 + size1` then the call `r.deallocate(ptr1, size1 + size2, alignment1)` is also valid and has the same effect as the two former calls (in some order).

To mark resource as sweeping one has to specialize the constant [**enable_sweeping_resource**](#enable_sweeping_resource) to true. If then the above condition is not satisfied, the behaviour of the program is undefined.

> [!NOTE]
> If a resource never returns adjacent regions from `allocate()` (and is not interchangeable with any resource that does) then it is automatically sweeping (since no two `deallocate()` calls for adjacent regions are valid in the first place).

---

### thread_safe_resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <typename R>
concept thread_safe_resource = resource<R> && enable_thread_safe_resource<R>;
```
The concept of a resource whose methods can safely be called concurrently from different threads.

To mark resource as thread_safe one has to specialize the constant [**enable_thread_safe_resource**](#enable_thread_safe_resource) to true. If then the above condition is not satisfied, the behaviour of the program is undefined.

---

### os_resource
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
class os_resource;
```
Memory resource that always allocates and frees memory via direct system calls to the OS.

#### Member functions

| Name | Description |
|---|---|
| [**allocate**](#os_resourceallocate) | allocates (full pages of) memory of the given size and alignment directly from the OS, using pages of the specified type |
| [**deallocate**](#os_resourcedeallocate) | deallocates the previously allocated region or several adjacent regions of memory |
| [**get_available_page_sizes**](#os_resourceget_available_page_sizes) | returns all the available page sizes on the system, that are known and supported in a **pow2_t**-valued range |
| [**get_big_page_size**](#os_resourceget_big_page_size) | get the size of a (default) big page if it is known and available on the system |
| [**get_page_size**](#os_resourceget_page_size) | get the size of a (regular) system memory page (usually 4KiB) |
| [**guaranteed_alignment**](#os_resourceguaranteed_alignment) | returns the known minimum alignment that any allocated with the specified page type address will have (regardless of the alignment argument) |
| [**min_size**](#os_resourcemin_size) | returns the known minimum size limit for allocations with the specified page type |
| **operator==** | the default equality comparison operator (always returns true) |
| **os_resource** | the default constructor (no-op) |

#### Constants

| Name | Description |
|---|---|
| **is_granular** | enables [**granular_resource**](#granular_resource) |
| **is_sweeping** | enables [**sweeping_resource**](#sweeping_resource) |
| **is_thread_safe** | enables [**thread_safe_resource**](#thread_safe_resource) |
