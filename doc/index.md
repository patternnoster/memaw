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
| [**pages_resource**](#pages_resource) | a wrapper around [**os_resource**](#os_resource), allocating memory directly from the OS using pages of the statically specified size |

### Resource aliases

| Name | Description |
|---|---|
| [**big_pages_resource**](#big_pages_resource) | a resource that allocates big (huge, large, super-) pages directly from the OS using system defaults |
| [**fixed_pages_resource**](#fixed_pages_resource) | a resource that allocates pages of the given fixed size directly from the OS |
| [**regular_pages_resource**](#regular_pages_resource) | a resource that allocates pages of regular system size directly from the OS |

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

### os_resource::allocate
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
template <page_type P = page_types::regular_t>
[[nodiscard]] static void* allocate(size_t size, size_t alignment = alignof(std::max_align_t),
                                    P page_type = {});
```
Allocates (full pages of) memory of the given size and alignment directly from the OS, using pages of the specified type.

The page type can be either:
* [**page_types::regular**](#page_types) to request regular memory pages of size [**get_page_size()**](#os_resourceget_page_size);
* [**page_types::big**](#page_types) to request the default big memory pages. On Linux the big (huge) pages of the default size (as possibly returned from [**get_big_page_size()**](#os_resourceget_big_page_size)) will be requested. Windows may (or may not) decide for itself what big (large) page sizes to use (and may even use several at a time) depending on the size and alignment values. For other systems, no promises of the real page type(s) are made;
* **pow2_t** with the exact page size value. On Linux (with procfs mounted at /proc) and Windows 10+ (or Server 2016+) must be one of the values returned from [**get_available_page_sizes()**](#os_resourceget_available_page_sizes). On other systems may be unsupported (and force the allocation to fail).

On Windows, an attempt to acquire the _SeLockMemoryPrivilege_ for the process will be made if this function is called with a non-regular page type. If that fails (e.g., if the user running the process doesn't have that privilege), all big (large) pages allocations will fail as well.

The alignment of the resulting address is at least as big as [**guaranteed_alignment(page_type)**](#os_resourceguaranteed_alignment). If the requested alignment is greater than that value, then note that:
* on Linux the allocation result is always aligned by the page size used (i.e., [**get_page_size()**](#os_resourceget_page_size) for [**page_types::regular**](#page_types), *[**get_big_page_size()**](#os_resourceget_big_page_size) for [**page_types::big**](#page_types) and the value of `page_type` otherwise), and the allocation with bigger alignment will fail since we choose to not implicitly allocate additional memory in that case;
* on latest versions of Windows (10+, Server 2016+), any alignment can be requested independent of the page type (but the allocation may still fail if no matching address is available). On earlier versions, only granularity (as returned by [**guaranteed_alignment(page_type)**](#os_resourceguaranteed_alignment)) alignment is supported (otherwise, the allocation will fail);
* on other systems, alignments bigger than the page size may or may not be supported (but note that if this implementation doesn't know how to guarantee the requested alignment on the current system, it will return nullptr without making a system call).

**Parameters**

* `size` must be a multiple of [**min_size(page_type)**](#os_resourcemin_size), otherwise the allocation will fail
* `alignment` must be a power of 2. On some systems (see above) the allocation will fail if this value is greater than [**guaranteed_alignment(page_type)**](#os_resourceguaranteed_alignment)
* `page_type` can be [**page_types::regular**](#page_types), [**page_types::big**](#page_types) or **pow2_t** with the exact page size value (see above for the detailed description and limitations)

---

<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
  template <__detail::same_as_either<page_types::regular_t,
                                     page_types::big_t> P>
  [[nodiscard]] static void* allocate(size_t size, P) noexcept;
```
An overload of the main allocation function that allows to avoid specifying alignment. See above for detailed description.

---

### os_resource::deallocate
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
static void deallocate(void* ptr, size_t size,
                       size_t /*alignment, ignored */= 1) noexcept;
```
Deallocates the previously allocated region or several adjacent regions of memory.

**Parameters**
* `ptr` must be the pointer to the beginning of the (first) region, returned from a previous call to [**allocate()**](#os_resourceallocate)
* `size` must be the value of the size argument passed to the [**allocate()**](#os_resourceallocate) call that resulted in allocation of the region (in case of a single region deallocation) or the exact sum of all those arguments (in case of several adjacent regions deallocation)
* `alignment` parameter is always ignored and present only for interface compatibility reasons

> [!WARNING]
> All memory in range [ptr, ptr + size) must be accessible (i.e., not previously deallocated), otherwise the behaviour is undefined.

---

### os_resource::get_available_page_sizes
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
static std::ranges::range auto get_available_page_sizes() noexcept;
```
Returns all the available page sizes on the system, that are known and supported in a **pow2_t**-valued range.

Always contains the regular page size (as returned from [**get_page_size()**](#os_resourceget_page_size)), and the result of [**get_big_page_size()**](#os_resourceget_big_page_size), if any. On Linux additionally returns all the supported huge page sizes (as listed in /sys/kernel/mm/hugepages/). On other systems, no additional entries are guaranteed, even if supported (but may still appear, like in case of Windows on **x86_64** with the **pdpe1gb** CPU flag)

> [!NOTE]
> This method enlisting a value does not guarantee a successful big page allocation of that size.

---

### os_resource::get_big_page_size
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
static std::optional<pow2_t> get_big_page_size() noexcept;
```
Get the size of a (default) big page if it is known and available on the system.

On Linux, returns the default big (huge) page size (as given in /proc/meminfo), if any. On Windows returns the minimum big (large) page size available (usually 2MiB). On other systems, may return an empty optional even if big pages are supported.

> [!NOTE]
> This method returning a value does not itself guarantee a successful big page allocation (e.g., Windows requires additional permissions, on Linux preallocated huge pages must be available etc.).

---

### os_resource::get_page_size
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
static pow2_t get_page_size() noexcept;
```
Get the size of a (regular) system memory page (usually 4KiB).

---

### os_resource::guaranteed_alignment
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
template <page_type P = page_types::regular_t>
static pow2_t guaranteed_alignment(P page_type = {}) noexcept;
```
Returns the known minimum alignment that any allocated with the specified page type address will have (regardless of the alignment argument).

> [!NOTE]
> The result is always >= [**min_size(page_type)**](#os_resourcemin_size), but may be bigger on some systems (e.g. with regular pages on Windows).

---

### os_resource::min_size
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
template <page_type P = page_types::regular_t>
static pow2_t min_size(P page_type = {}) noexcept;
```
Returns the known minimum size limit for allocations with the specified page type.

**Return value**

* for [**page_types::regular**](#page_typesregular), [**get_page_size()**](#os_resourceget_page_size);
* for [**page_types::big**](#page_typesbig), [**get_big_page_size()**](#os_resourceget_big_page_size) if it's defined, or of [**get_page_size()**](#os_resourceget_page_size) if it's not;
* for explicitly specified page size, its value.

---

### page_type
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
template <typename T>
concept page_type =
  __detail::same_as_either<T, page_types::regular_t, page_types::big_t, pow2_t>;
```
The concept of a type that denotes the size of a system memory page and must be either one of the tags in [**page_types**](#page_types) or **pow2_t** for explicit size specification.

---

### page_types
<sub>Defined in header [&lt;memaw/os_resource.hpp&gt;](/include/memaw/os_resource.hpp)</sub>
```c++
struct page_types;
```
Tags for the types of system memory pages available for allocation.

#### Constants

| Name | Description |
|---|---|
| **big** | a (constexpr static) constant of type **page_types::big_t** to denote big pages |
| **regular** | a  (constexpr static) constant of type **page_types::regular_t** to denote regular pages |

---

### pages_resource
<sub>Defined in header [&lt;memaw/pages_resource.hpp&gt;](/include/memaw/pages_resource.hpp)</sub>
```c++
template <page_type auto _type>
class pages_resource;
```
A wrapper around [**os_resource**](#os_resource), allocating memory directly from the OS using pages of the statically specified size.

#### Member functions

| Name | Description |
|---|---|
| [**allocate**](#pages_resourceallocate) | allocate a region of memory of the given size and alignment with a direct syscall |
| [**deallocate**](#pages_resourcedeallocate) | deallocate a previously allocated region of memory |
| [**guaranteed_alignment**](#pages_resourceguaranteed_alignment) | get the minimum alignment every allocated address has |
| [**min_size**](#pages_resourcemin_size) | get the minimum allocation size for this resource |
| **operator==** | the default equality comparison operator (always returns true) |
| **pages_resource** | the default constructor (no-op) |

#### Constants

| Name | Description |
|---|---|
| **is_granular** | enables [**granular_resource**](#granular_resource) |
| **is_sweeping** | enables [**sweeping_resource**](#sweeping_resource) |
| **is_thread_safe** | enables [**thread_safe_resource**](#thread_safe_resource) |

### pages_resource::allocate
<sub>Defined in header [&lt;memaw/pages_resource.hpp&gt;](/include/memaw/pages_resource.hpp)</sub>
```c++
[[nodiscard]] static void* allocate(size_t size,
                                    size_t alignment = alignof(std::max_align_t)) noexcept;
```
Allocate a region of memory of the given size and alignment with a direct syscall (see [**os_resource::allocate()**](#os_resourceallocate) for details).

---

### pages_resource::deallocate
<sub>Defined in header [&lt;memaw/pages_resource.hpp&gt;](/include/memaw/pages_resource.hpp)</sub>
```c++
static void deallocate(void* ptr, size_t size,
                       size_t /*alignment, ignored */= 1) noexcept;
```
Deallocate a previously allocated region of memory (see [**os_resource::deallocate()**](#os_resourcedeallocate) for details).

---

### pages_resource::guaranteed_alignment
<sub>Defined in header [&lt;memaw/pages_resource.hpp&gt;](/include/memaw/pages_resource.hpp)</sub>
```c++
constexpr static pow2_t guaranteed_alignment() noexcept requires(explicit_size);

static pow2_t guaranteed_alignment() noexcept requires(!explicit_size);
```
Get the minimum alignment every allocated address has.

---

### big_pages_resource
<sub>Defined in header [&lt;memaw/pages_resource.hpp&gt;](/include/memaw/pages_resource.hpp)</sub>
```c++
using big_pages_resource = pages_resource<page_types::big>;
```
A resource that allocates big (huge, large, super-) pages directly from the OS using system defaults.

---

### fixed_pages_resource
<sub>Defined in header [&lt;memaw/pages_resource.hpp&gt;](/include/memaw/pages_resource.hpp)</sub>
```c++
template <size_t _size> requires(is_pow2(_size))
using fixed_pages_resource = pages_resource<pow2_t(_size, pow2_t::exact)>;
```
A resource that allocates pages of the given fixed size directly from the OS.

---

### regular_pages_resource
<sub>Defined in header [&lt;memaw/pages_resource.hpp&gt;](/include/memaw/pages_resource.hpp)</sub>
```c++
using regular_pages_resource = pages_resource<page_types::regular>;
```
A resource that allocates pages of regular system size directly from the OS (same as [**os_resource**](#os_resource)).

---

### pages_resource::min_size
<sub>Defined in header [&lt;memaw/pages_resource.hpp&gt;](/include/memaw/pages_resource.hpp)</sub>
```c++
constexpr static pow2_t min_size() noexcept requires(explicit_size);

static pow2_t min_size() noexcept requires(!explicit_size);
```
Get the minimum allocation size for this resource.

---

### enable_granular_resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <bound_resource R>
constexpr bool enable_granular_resource = requires {
  { std::bool_constant<R::is_granular>{} } -> std::same_as<std::true_type>;
};
```
A specializable global constant that enables the [**granular_resource**](#granular_resource) concept (see below) for R. By default, true iff R defines a constexpr member `R::is_granular` implicitly convertible to true.

---

### enable_sweeping_resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <resource R>
constexpr bool enable_sweeping_resource = requires {
  { std::bool_constant<R::is_sweeping>{} } -> std::same_as<std::true_type>;
};
```
A specializable global constant that enables the [**sweeping_resource**](#sweeping_resource) concept (see below) for R. By default, true iff R defines a constexpr member `R::is_sweeping` implicitly convertible to true.

---

### enable_thread_safe_resource
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <resource R>
constexpr bool enable_thread_safe_resource = requires {
  { std::bool_constant<R::is_thread_safe>{} } -> std::same_as<std::true_type>;
};
```
A specializable global constant that enables the [**thread_safe_resource**](#thread_safe_resource) concept (see below) for R. By default, true iff R defines a constexpr member `R::is_thread_safe` implicitly convertible to true.
