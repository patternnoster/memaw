# Memaw

The library is still work in progress. See below for the list of features implemented so far.

## Members
### Concepts

| Name | Description |
|---|---|
| [**resource**](#resource) | the basic concept of a memory resource: can allocate and deallocate memory given size and alignment |
| [**bound_resource**](#bound_resource) | the concept of a resource that has a (constant) minimum allocation size limit |
| [**granular_resource**](#granular_resource) | the concept of a bound resource that can only allocate sizes that are multiples of its minimum allocation |
| [**interchangeable_resource_with**](#interchangeable_resource_with) | the concept of resources any two instances of which can safely deallocate memory allocated by the other |
| [**nothrow_resource**](#nothrow_resource) | the concept of a resource whose allocation, deallocation and equality testing methods don't throw exceptions |
| [**overaligning_resource**](#overaligning_resource) | the concept of a resource that has a (constant) guaranteed alignment greater than `alignof(std::max_align_t)` |
| [**substitutable_resource_for**](#substitutable_resource_for) | the concept of a resource that can safely accept all deallocation calls from a particular instance of another resource |
| [**sweeping_resource**](#sweeping_resource) | the concept of a resource that permits combining deallocations of adjacent regions into one call |
| [**thread_safe_resource**](#thread_safe_resource) | the concept of a resource whose methods can safely be called concurrently from different threads |

### Memory resources

| Name | Description |
|---|---|
| [**cache_resource**](#cache_resource) | memory resource that allocates big blocks from an upstream resource and uses those blocks for (smaller) allocation requests |
| [**chain_resource**](#chain_resource) | memory resource adaptor that sequentially tries every resource in a given list until a successful allocation |
| [**os_resource**](#os_resource) | memory resource that always allocates and frees memory via direct system calls to the OS |
| [**pages_resource**](#pages_resource) | a wrapper around [**os_resource**](#os_resource), allocating memory directly from the OS using pages of the statically specified size |
| [**pool_resource**](#pool_resource) | memory resource that maintains lists of chunks of fixed sizes allocated from the upstream resource |

### Resource aliases

| Name | Description |
|---|---|
| [**big_pages_resource**](#big_pages_resource) | a resource that allocates big (huge, large, super-) pages directly from the OS using system defaults |
| [**fixed_pages_resource**](#fixed_pages_resource) | a resource that allocates pages of the given fixed size directly from the OS |
| [**regular_pages_resource**](#regular_pages_resource) | a resource that allocates pages of regular system size directly from the OS |

### Traits

| Name | Description |
|---|---|
| [**resource_traits**](#resource_traits) | a structure containing traits for memory resource concepts defined by this library |

### Functions

| Name | Description |
|---|---|
| [**allocate**](#allocate) | allocates memory from the given resource with the chosen exception policy |
| [**allocate_at_least**](#allocate_at_least) | allocates memory from the given resource with the chosen exception policy, while increasing the requested size if necessary |
| [**deallocate**](#deallocate) | deallocates memory to the given resource with the chosen exception policy |

### Helper concepts and types

| Name | Description |
|---|---|
| [**allocation_result**](#allocation_result) | the structure used to return resource allocation result of the implementation defined real size (e.g. from [**allocate_at_least()**](#allocate_at_least)) |
| [**exceptions_policy**](#exceptions_policy) | specifies the requested exceptions policy for the global [**allocate()**](#allocate)/[**deallocate()**](#deallocate) calls |
| [**page_type**](#page_type) | the concept of a type that denotes the size of a system memory page and must be either one of the tags in [**page_types**](#page_types) or **pow2_t** for explicit size specification |
| [**page_types**](#page_types) | tags for the types of system memory pages available for allocation |

### Global variables

| Name | Description |
|---|---|
| [**enable_granular_resource**](#enable_granular_resource) | a specializable global constant that enables the [**granular_resource**](#granular_resource) concept |
| [**enable_interchangeable_resources**](#enable_interchangeable_resources) | a specializable global constant that enables the [**interchangeable_resource_with**](#interchangeable_resource_with) concept |
| [**enable_substitutable_resource_for**](#enable_substitutable_resource_for) | a specializable global constant that enables the [**substitutable_resource_for**](#substitutable_resource_for) concept |
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

### interchangeable_resource_with
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <typename R1, typename R2>
concept interchangeable_resource_with = resource<R1> && resource<R2>
  && ((std::same_as<R1, R2> && __detail::equal_instances<R1>)
      || enable_interchangeable_resources<R1, R2>
      || enable_interchangeable_resources<R2, R1>);
```
The concept of resources any two instances of which can safely deallocate memory allocated by the other.

To mark resources as interchangeable one has to specialize the constant [**enable_interchangeable_resources**](#enable_interchangeable_resources) to `true`. If then the above condition is not satisfied, the behaviour of the program is undefined.
Alternatively to mark a resource as interchangeable with itself (meaning any two instances of it can process each other deallocation calls) one can make its `operator==` and `operator!=` return a type implicitly and constexpr-convertible to true and false respectively (e.g., **std::(true/false)_type**).

> [!NOTE]
> Interchangeability is not an equivalence relation: while symmetric, it is neither transitive nor even necessarily reflexive.

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

### substitutable_resource_for
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <typename R1, typename R2>
concept substitutable_resource_for = resource<R1> && resource<R2>
  && (interchangeable_resource_with<R1, R2>
      || enable_substitutable_resource_for<R1, R2>);
```
The concept of a resource that can safely accept all deallocation calls from a particular instance of another resource.

More formally, given instances `r1` and `r2`, for every valid call `r2.deallocate(ptr, size, alignment)` a call `r1.deallocate(ptr, size, alignment)` is also valid, assuming no calls to `r2.deallocate()` has been made, and all the memory is freed by the time both destructors return.
Obviously, if R1 and R2 are interchangeable, then this is always true. If they are not then one can specialize the global constant [**enable_substitutable_resource_for**](#enable_substitutable_resource_for) to make a resource substitutable for another (in case the semantic requirement above is met)

> [!NOTE]
> This is a much weaker concept than [**interchangeable_resource_with**](#interchangeable_resource_with). It is not symmetric and only requires validity when all (and not some) of the deallocation calls from a particular instance of R2 are redirected to a particular instance of R1.

> [!NOTE]
> If R1 is sweeping, then it must accept mixed adjacent regions from both resources (if possible).

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

### cache_resource
<sub>Defined in header [&lt;memaw/cache_resource.hpp&gt;](/include/memaw/cache_resource.hpp)</sub>
```c++
template <sweeping_resource R,
          cache_resource_config _config = cache_resource_config{
            .thread_safe = thread_safe_resource<R>
          }>
class cache_resource;
```
Memory resource that allocates big blocks from an upstream resource and uses those blocks for (smaller) allocation requests. Memory is not freed until the resource is destructed (as in **std::pmr::monotonic_buffer_resource**).

#### Member functions

| Name | Description |
|---|---|
| [**allocate**](#cache_resourceallocate) | allocates memory from the cache, calling the upstream **allocate()** if there is not enough left |
| [**cache_resource**](#cache_resourcecache_resource) | constructs the cache with the upstream resource default or move-constructed |
| [**deallocate**](#cache_resourcedeallocate) | deallocates previously allocated memory (no call to the upstream here) |
| [**guaranteed_alignment**](#cache_resourceguaranteed_alignment) | returns the minimal alignment of any address allocated by the cache if its configuration allows that |
| [**min_size**](#cache_resourcemin_size) | returns the (configured) size of a minimum allocation: any allocation can only request a size that is a multiple of this value |
| **operator==** | the equality comparison operator only returning true for same instances |
| **~cache_resource** | calls **deallocate()** on the upstream resource for all previously deallocated cache memory |

#### Member types

| Name | Description |
|---|---|
| **upstream_t** | the underlying resource to cache (a template parameter) |

#### Constants

| Name | Description |
|---|---|
| **config** | the resource config of type [**cache_resource_config**](#cache_resource_config) (template parameter) |
| **is_granular** | enables [**granular_resource**](#granular_resource) |
| **is_substitutable_for** | enables [**substitutable_resource_for**](#substitutable_resource_for) if the upstream resource is substitutable for the given one |
| **is_sweeping** | enables [**sweeping_resource**](#sweeping_resource) |
| **is_thread_safe** | enables [**thread_safe_resource**](#thread_safe_resource) if [**config.thread_safe**](#cache_resource_configthread_safe) is true and the upstream resource is thread-safe |
| **min_granularity** | the minimum value for the granularity parameter. Normally equals 32 bytes, always `>= alignof(std::max_align_t)` |

#### Helper types and specializations

| Name | Description |
|---|---|
| [**cache_resource_config**](#cache_resource_config) | configuration parameters for [**cache_resource**](#cache_resource) with valid defaults |
| **enable_substitutable_resource_for** | enables the [**substitutable_resource_for**](#substitutable_resource_for) concept for two **cache_resources** if their corresponding upstream resources are substitutable |

### cache_resource::allocate
<sub>Defined in header [&lt;memaw/cache_resource.hpp&gt;](/include/memaw/cache_resource.hpp)</sub>
```c++
[[nodiscard]] void* allocate
  (size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;
```
Allocates memory from the cache, calling the upstream **allocate()** if there is not enough left.

**Parameters**
* `size` must be a multiple of the configured granularity
* `alignment` must be a power of 2. If it is greater than the configured granularity, additional padding will be used

---

### cache_resource::cache_resource
<sub>Defined in header [&lt;memaw/cache_resource.hpp&gt;](/include/memaw/cache_resource.hpp)</sub>
```c++
constexpr cache_resource()
  noexcept(std::is_nothrow_default_constructible_v<upstream_t>)
  requires(std::default_initializable<upstream_t>);
```

---

<sub>Defined in header [&lt;memaw/cache_resource.hpp&gt;](/include/memaw/cache_resource.hpp)</sub>
```c++
constexpr cache_resource(upstream_t&& upstream)
  noexcept(std::is_nothrow_move_constructible_v<upstream_t>);
```

---

<sub>Defined in header [&lt;memaw/cache_resource.hpp&gt;](/include/memaw/cache_resource.hpp)</sub>
```c++
constexpr cache_resource(cache_resource&& rhs)
  noexcept(std::is_nothrow_move_constructible_v<upstream_t>);
```
Move constructs the cache leaving rhs in an empty but valid state (as long as the same is true for the upstream resource).

> [!NOTE]
> The operation is not thread safe even if the resources are configured as such. All memory allocated by the rhs resource and not deallocated before the move must be deallocated through the new instance.

---

### cache_resource::deallocate
<sub>Defined in header [&lt;memaw/cache_resource.hpp&gt;](/include/memaw/cache_resource.hpp)</sub>
```c++
void deallocate(void* ptr, size_t size,
                size_t alignment = alignof(std::max_align_t)) noexcept;
```
Deallocates previously allocated memory. The **deallocate()** call on the upstream resource happens only on destruction.

---

### cache_resource::guaranteed_alignment
<sub>Defined in header [&lt;memaw/cache_resource.hpp&gt;](/include/memaw/cache_resource.hpp)</sub>
```c++
constexpr static pow2_t guaranteed_alignment() noexcept
  requires(config.granularity > alignof(std::max_align_t));
```
Returns the minimal alignment of any address allocated by the cache if its configuration allows that.

---

### cache_resource::min_size
<sub>Defined in header [&lt;memaw/cache_resource.hpp&gt;](/include/memaw/cache_resource.hpp)</sub>
```c++
constexpr static pow2_t min_size() noexcept;
```
Returns the (configured) size of a minimum allocation: any allocation can only request a size that is a multiple of this value.

---

### cache_resource_config
<sub>Defined in header [&lt;memaw/cache_resource.hpp&gt;](/include/memaw/cache_resource.hpp)</sub>
```c++
struct cache_resource_config;
```
Configuration parameters for [**cache_resource**](#cache_resource) with valid defaults.

#### Fields

| Name | Default | Description |
|---|:---:|---|
| [**granularity**](#cache_resource_configgranularity) | 4KiB | the allocation granularity for this adaptor, the size of every allocation must be a multiple of this value |
| [**min_block_size**](#cache_resource_configmin_block_size) | 32MiB | the minimum (and also the initial) block size to allocate from the underlying resource |
| [**max_block_size**](#cache_resource_configmax_block_size) | 1GiB | the maximum block size to allocate from the underlying resource |
| [**block_size_multiplier**](#cache_resource_configblock_size_multiplier) | 2.0 | the multiplier for every next block allocation from the underlying resource (until **max_block_size** is reached) |
| [**thread_safe**](#cache_resource_configthread_safe) | true | thread safety policy |

#### cache_resource_config::granularity
```c++
const pow2_t granularity = pow2_t{4_KiB};
```
The allocation granularity for this adaptor. The size of every allocation must be a multiple of this value. Must not be smaller than [**cache_resource::min_granularity**](#cache_resource).

> [!NOTE]
> This value also determines the guaranteed alignment and the alignment parameter passed to the upstream **allocate()** call.

---

#### cache_resource_config::min_block_size
```c++
const size_t min_block_size = 32_MiB;
```
The minimum (and also the initial) block size to allocate from the underlying resource. Must not be smaller than granularity.

> [!NOTE]
> If the upstream is a [**bound_resource**](#bound_resource), any block size value will be automatically ceiled to be greater than its **min_size()** (and additionally a multiple of it if the upstream is also a [**granular_resource**](#granular_resource)) at runtime.

---

#### cache_resource_config::max_block_size
```c++
const size_t max_block_size = 1_GiB;
```
The maximum block size to allocate from the underlying resource (must not be smaller than **min_block_size**).

> [!NOTE]
> If an allocation fails at some point, a previous (smaller by **block_size_multiplier** to some power, but not less than **min_block_size**) size will be tried if possible. That will require additional upstream [**allocate()**](#allocate) calls.

---

#### cache_resource_config::block_size_multiplier
```c++
const double block_size_multiplier = 2.0;
```
The multiplier for every next block allocation from the underlying resource (until **max_block_size** is reached), must be greater than 1, unless the **min_** and **max_block_size** are equal (in which case this value is ignored).

> [!NOTE]
> Sequential increase is intended but not guaranteed. At any point the implementation may choose to allocate a block of any size `min_block_size * block_size_multiplier^n` for some integral `n >= 0`, but not greater than **max_block_size**.

---

#### cache_resource_config::thread_safe
```c++
const bool thread_safe = true;
```
Thread safety policy: if set to true, the implementation will use atomic instructions to manage its internal structures (requires DWCAS). The thread safe implementation is lock-free.

> [!NOTE]
> If the upstream resource is not thread safe, setting this parameter to true will change the type of instructions used but won't make the cache thread safe either.

---

### chain_resource
<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
template <resource... Rs> requires(sizeof...(Rs) > 0)
class chain_resource;
```
Memory resource adaptor that sequentially tries every resource in a given list until a successful allocation.

The [**deallocate()**](#chain_resourcedeallocate) call to the chain will, by default, be forwarded to the first resource in the list that is [**substitutable_for**](#substitutable_resource_for) all the other resources. The [**dispatch_deallocate()**](#chain_resourcedispatch_deallocate) template (see below) can be specialized to alter that behaviour.

If no such resource and no specialization of the dispatch template exists, the default [**deallocate()**](#chain_resourcedeallocate) method will be unavailable (thus, the chain won't model the [**resource**](#resource) concept) and one should use the [**deallocate_with**](#chain_resourcedeallocate_with) method (that takes an additional index argument) instead.

#### Member functions

| Name | Description |
|---|---|
| [**allocate**](#chain_resourceallocate) | calls **allocate()** with the given size and alignment arguments on every resource in the list until the first success |
| [**chain_resource**](#chain_resourcechain_resource) | constructs the chain with the link resources default or move-constructed |
| [**deallocate**](#chain_resourcedeallocate) | deallocates memory previously allocated by the chain by forwarding the call to one of the resources in it |
| [**deallocate_with**](#chain_resourcedeallocate_with) | deallocates memory previously allocated by the chain by forwarding the call to the resource at the given index |
| [**do_allocate**](#chain_resourcedo_allocate) | same as **allocate()** but returns a pair of the resulting pointer and the index of the resource that performed the allocation |
| [**guaranteed_alignment**](#chain_resourceguaranteed_alignment) | returns the guaranteed alignment of all the memory addresses allocated by the chain, defined iff all of the resources in the chain are overaligning |
| [**min_size**](#chain_resourcemin_size) | returns the minimum allocation size, defined iff any of the resources in the chain are bound |
| **operator==** | the default equality comparison operator (the conjunction of the equalities of links) |

#### Constants

| Name | Description |
|---|---|
| **is_granular** | specifies if the chain is [**granular**](#granular_resource) (i.e., at least one of its resources is granular) |
| **is_interchangeable_with** | specifies if the chain is [**interchangeable**](#interchangeable_resource_with) with the given resource (i.e., all of its resources are interchangeable with it) |
| **is_thread_safe** | specifies if the chain is [**thread_safe**](#thread_safe_resource) (i.e., all of its resources are thread safe) |

#### Friends and Specializations

| Name | Description |
|---|---|
| [**dispatch_deallocate**](#chain_resourcedispatch_deallocate) | Chooses the resource in the chain that will service the [**deallocate()**](#chain_resourcedeallocate) call |
| **enable_substitutable_resource_for** | specifies if a resource is [**substitutable**](#substitutable_resource_for) for a chain (i.e., if it is substitutable for all of its resources) |
| **enable_substitutable_resource_for** | enables the [**substitutable_resource_for**](#substitutable_resource_for) concept for chains with the constant deallocator if the corresponding resource is substitutable for the given one |
| **enable_sweeping_resource** |  enables the [**sweeping_resource**](#sweeping_resource) concept for chains with the constant deallocator if the corresponding resource is sweeping |

### chain_resource::allocate
<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
[[nodiscard]] void* allocate
  (size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;
```
Calls **allocate()** with the given size and alignment arguments on every resource in the list until the first success (i.e., a call that returned a non-null pointer) and returns that result. Returns nullptr if all of the calls have failed.

---

### chain_resource::chain_resource
<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
constexpr chain_resource()
  noexcept((std::is_nothrow_default_constructible_v<Rs> && ...))
  requires((std::default_initializable<Rs> && ...));
```
Constructs the chain calling the link resources default constructors.

---

<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
constexpr chain_resource(Rs&&... resources)
  noexcept((std::is_nothrow_move_constructible_v<Rs> && ...))
  requires((std::move_constructible<Rs> && ...));
```
Constructs the chain calling the link resources move constructors.

---

### chain_resource::deallocate
<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
void deallocate(void* ptr, size_t size,
                size_t alignment = alignof(std::max_align_t))
  noexcept((__detail::has_constant_dispatcher<chain_resource>
            && __detail::has_nothrow_deallocate
                 <__detail::at<__detail::dispatch<chain_resource>, Rs...>>)
           || (__detail::has_nothrow_dispatcher<chain_resource>
               && ... && __detail::has_nothrow_deallocate<Rs>))
  requires(__detail::has_dispatcher<chain_resource>);
```
Deallocates memory previously allocated by the chain by forwarding the call to one of the resources in it, using the [**dispatch_deallocate()**](#chain_resourcedispatch_deallocate) (see above) function to get its index, and is only defined if such method is declared for the chain.

---

### chain_resource::deallocate_with
<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
void deallocate_with(size_t idx, void* ptr, size_t size,
                     size_t alignment = alignof(std::max_align_t))
  noexcept((__detail::has_nothrow_deallocate<Rs> && ...));
```
Deallocates memory previously allocated by the chain by forwarding the call to the resource at the given index.

---

### chain_resource::do_allocate
<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
[[nodiscard]] std::pair<void*, size_t> do_allocate
  (size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;
```
Same as [**allocate**](#chain_resourceallocate) but returns a pair of the resulting pointer and the index of the resource that performed the allocation.

> [!NOTE]
> If the resulting pointer is null (i.e., if all the resources have been tried and have failed), the index of the last resource is always returned, even if that call threw an exception.

---

### chain_resource::guaranteed_alignment
<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
constexpr static pow2_t guaranteed_alignment() noexcept
  requires(overaligning_resource<Rs> && ...);
```
Returns the guaranteed alignment of all the memory addresses allocated by the chain, defined iff all of the resources in the chain are overaligning (and equals the minimum of all their **guaranteed_alignment()**).

---

### chain_resource::min_size
<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
constexpr static size_t min_size() noexcept
  requires(bound_resource<Rs> || ...);
```
Returns the minimum allocation size, defined iff any of the resources in the chain are bound.

If defined, equals the smallest number n such that:
1. for any [**bound_resource**](#bound_resource) R in the chain, `n >= R::min_size()`;
1. for any [**granular_resource**](#granular_resource) R in the chain, n is also a multiple of `R::min_size()`.

> [!NOTE]
> If such n is not representable as `size_t`, the behaviour is undefined.

---

### chain_resource::dispatch_deallocate
<sub>Defined in header [&lt;memaw/chain_resource.hpp&gt;](/include/memaw/chain_resource.hpp)</sub>
```c++
template <resource... Us>
  requires(__detail::resource_list<Us...>::has_universal_deallocator)
std::integral_constant<size_t, __detail::resource_list<Us...>::universal_deallocator_id>
  dispatch_deallocate(const chain_resource<Us...>&, void*, size_t, size_t);
```
Chooses the resource in the chain that will service the [**deallocate()**](#chain_resourcedeallocate) call.

By default, only declared for chains that have a resource that is [**substitutable_for**](#substitutable_resource_for) all the other resources (in which case returns the index of the first such resource). Can be overloaded, returning any type convertible to `size_t`.

If the index is constant and known at compile-time (as in the default scenario), `std::integral_constant` (or a similar type) should be returned as it allows to deduce some compile-time features (e.g., if the chain models **substitutable_for** or if the deallocation is noexcept in case that's not true for all the resources in the chain), and, additionally, can lead to generating more efficient code on some compilers (e.g., MSVC).

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

### allocation_result
<sub>Defined in header [&lt;memaw/resource_traits.hpp&gt;](/include/memaw/resource_traits.hpp)</sub>
```c++
struct allocation_result {
  void* ptr;
  size_t size;
};
```
The structure used to return resource allocation result of the implementation defined real size (e.g. from [**allocate_at_least()**](#allocate_at_least)).

---

### exceptions_policy
<sub>Defined in header [&lt;memaw/resource_traits.hpp&gt;](/include/memaw/resource_traits.hpp)</sub>
```c++
enum class exceptions_policy;
```
Specifies the requested exceptions policy for the global [**allocate()**](#allocate)/[**deallocate()**](#deallocate) calls.

#### Enumerators

| Name | Description |
|---|---|
| **original** | preserve the original exceptions policy of the resource |
| **nothrow** | suppress exceptions that may be thrown by the resource |
| **throw_bad_alloc** | (allocation only) force throw `std::bad_alloc` if the resource returns **nullptr** |

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
| **is_interchangeable_with** | enables [**interchangeable_resource_with**](#interchangeable_resource_with) the [**os_resource**](#os_resource) and any [**pages_resource**](#pages_resource) |
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

### pages_resource::min_size
<sub>Defined in header [&lt;memaw/pages_resource.hpp&gt;](/include/memaw/pages_resource.hpp)</sub>
```c++
constexpr static pow2_t min_size() noexcept requires(explicit_size);

static pow2_t min_size() noexcept requires(!explicit_size);
```
Get the minimum allocation size for this resource.

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

### pool_resource
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
template <sweeping_resource R,
          pool_resource_config _config = pool_resource_config{
            .thread_safe = thread_safe_resource<R>
          }>
class pool_resource;
```
Memory resource that maintains lists of chunks of fixed sizes allocated from the upstream resource. Chunks are reused upon deallocation but are not returned to the upstream until this resource is destructed.

#### Member functions
| Name | Description |
|---|---|
| [**allocate**](#pool_resourceallocate) | allocates memory from the pool, calling the upstream **allocate()** if there is not enough left |
| [**deallocate**](#pool_resourcedeallocate) | deallocates previously allocated chunks and marks them for reuse |
| [**guaranteed_alignment**](#pool_resourceguaranteed_alignment) | returns the minimal alignment of any address allocated by the pool if its configuration allows that |
| [**min_size**](#pool_resourcemin_size) | returns the (configured) size of a minimum allocation: any allocation can only request a size that is a multiple of this value |
| **operator==** | the equality comparison operator only returning true for same instances |
| [**pool_resource**](#pool_resourcepool_resource) | constructs the pool with the upstream resource default or move-constructed |
| **~pool_resource** | calls **deallocate()** on the upstream resource for all previously allocated memory |

#### Member types

| Name | Description |
|---|---|
| **upstream_t** | the underlying resource to allocate memory from |

#### Constants

| Name | Description |
|---|---|
| **chunk_sizes** | the `std::range` of sizes of chunks in the pool |
| **config** | the configuration parameters of the resource of type [**pool_resource_config**](#pool_resource_config) (template parameter) |
| **is_granular** | enables [**granular_resource**](#granular_resource) |
| **is_sweeping** | enables [**sweeping_resource**](#sweeping_resource) |
| **is_thread_safe** | enables [**thread_safe_resource**](#thread_safe_resource) if [**config.thread_safe**](#pool_resource_configthread_safe) is true and the upstream resource is thread-safe |

#### Helper types
| Name | Description |
|---|---|
| [**pool_resource_config**](#pool_resource_config) | configuration parameters for [**pool_resource**](#pool_resource) with valid defaults |

### pool_resource::allocate
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
[[nodiscard]] void* allocate
  (size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;
```
Allocates memory from the pool, calling the upstream [**allocate()**](#allocate) if there is not enough left.

**Parameters**
* `size` must be a multiple of [**config.min_chunk_size**](#pool_resource_configmin_chunk_size)
* `alignment` must be a power of 2

---

### pool_resource::deallocate
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
void deallocate(void* ptr, size_t size,
                size_t = alignof(std::max_align_t)) noexcept;
```
Deallocates previously allocated chunks and marks them for reuse. The **deallocate()** call on the upstream resource happens only on destruction.

---

### pool_resource::guaranteed_alignment
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
constexpr static pow2_t guaranteed_alignment() noexcept
  requires(_config.min_chunk_size > alignof(std::max_align_t));
```
Returns the minimal alignment of any address allocated by the pool if its configuration allows that.

---

### pool_resource::min_size
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
constexpr static pow2_t min_size() noexcept;
```
Returns the (configured) size of a minimum allocation: any allocation can only request a size that is a multiple of this value.

---

### pool_resource::pool_resource
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
constexpr pool_resource()
  noexcept(std::is_nothrow_default_constructible_v<upstream_t>)
  requires(std::default_initializable<upstream_t>);
```

---

<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
constexpr pool_resource(upstream_t&& upstream)
  noexcept(std::is_nothrow_move_constructible_v<upstream_t>);
```

---

<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
constexpr pool_resource(pool_resource&& rhs)
  noexcept(std::is_nothrow_move_constructible_v<upstream_t>);
```
Move constructs the pool leaving rhs in an empty but valid state (as long as the same is true for the upstream resource).

> [!NOTE]
> The operation is not thread safe even if the resources are configured as such. All memory allocated by the rhs resource and not deallocated before the move must be deallocated through the new instance.

---

### pool_resource_config
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
struct pool_resource_config;
```
Configuration parameters for [**pool_resource**](#pool_resource) with valid defaults.

| Name | Default | Description |
|---|:---:|---|
| [**min_chunk_size**](#pool_resource_configmin_chunk_size) | 1KiB | the minimum size of a chunk, the size of every allocation must be a multiple of this value (since every allocation must request full chunks) |
| [**max_chunk_size**](#pool_resource_configmax_chunk_size) | 16KiB | the maximum size of a chunk |
| [**chunk_size_multiplier**](#pool_resource_configchunk_size_multiplier) | 2 | the multiplier of chunk sizes, determines the number of chunk lists maintained and the size of allocation from the upstream |
| [**thread_safe**](#pool_resource_configthread_safe) | true | thread safety policy |

### pool_resource_config::min_chunk_size
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
const pow2_t min_chunk_size = pow2_t{1_KiB};
```
The minimum size of a chunk. The size of every allocation must be a multiple of this value (since every allocation must request full chunks).

> [!NOTE]
> This value also determines the guaranteed alignment and the alignment parameter passed to the upstream **allocate()** call.

---

### pool_resource_config::max_chunk_size
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
const size_t max_chunk_size = 16_KiB;
```
The maximum size of a chunk. Must equal `min_chunk_size * chunk_size_multiplier^n` for some `n`.

---

### pool_resource_config::chunk_size_multiplier
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
const size_t chunk_size_multiplier = 2;
```
The multiplier of chunk sizes. Determines the number of chunk lists maintained and the size of allocation from the upstream (which is `max_chunk_size * chunk_size_multiplier` normally).

---

### pool_resource_config::thread_safe
<sub>Defined in header [&lt;memaw/pool_resource.hpp&gt;](/include/memaw/pool_resource.hpp)</sub>
```c++
const bool thread_safe = true;
```
Thread safety policy: if set to true, the implementation will use atomic instructions to manage its internal structures (requires DWCAS). The thread safe implementation is lock-free.

> [!NOTE]
> If the underlying resource is not thread safe, setting this parameter to true will change the type of instructions used but won't make the pool thread safe either.

---

### resource_traits
<sub>Defined in header [&lt;memaw/resource_traits.hpp&gt;](/include/memaw/resource_traits.hpp)</sub>
```c++
template <resource R>
struct resource_traits;
```
A structure containing traits for memory resource concepts defined by this library.

#### Constants

| Name | Description |
|---|---|
| **is_bound** | true iff the resource has a minimal allocation size limit (see the [**bound_resource**](#bound_resource) concept for details) |
| **is_granular** | true iff the resource can only allocate multiples of its minimum allocation size (see the [**granular_resource**](#granular_resource) concept for details) |
| **is_interchangeable_with** | true iff the resource and the provided one can always safely deallocate memory allocated by the other (see the [**interchangeable_resource_with**](#interchangeable_resource_with) concept for details) |
| **is_nothrow** | true iff the resource's methods don't throw exceptions (see the [**nothrow_resource**](#nothrow_resource) concept for details) |
| **is_overaligning** | true iff the resource has a guaranteed alignment more than `alignof(std::max_align_t)` (see the [**overaligning_resource**](#overaligning_resource) concept for details) |
| **is_substitutable_for** | true iff the resource can safely deallocate all memory allocated by any particalar instance of the provided one (see the [**substitutable_resource_for**](#substitutable_resource_for) concept for details) |
| **is_sweeping** | true iff the resource can deallocate adjacent regions with a single call (see the [**sweeping_resource**](#sweeping_resource) concept for details) |
| **is_thread_safe** | true iff the resource is thread safe (see the [**thread_safe_resource**](#thread_safe_resource) concept for details) |

#### Member functions

| Name | Description |
|---|---|
| [**ceil_allocation_size**](#resource_traitsceil_allocation_size) | returns the minimum allocation size that can be requested from the resource and is not less than the provided argument |
| [**guaranteed_alignment**](#resource_traitsguaranteed_alignment) | gets the minimum alignment of every allocation by the resource |
| [**min_size**](#resource_traitsmin_size) | gets the minimum allocation size limit for the resource |

### resource_traits::ceil_allocation_size
<sub>Defined in header [&lt;memaw/resource_traits.hpp&gt;](/include/memaw/resource_traits.hpp)</sub>
```c++
constexpr static size_t ceil_allocation_size(size_t size) noexcept;
```
Returns the minimum allocation size that can be requested from the resource and is not less than the provided argument.

If R models [**granular_resource**](#granular_resource), returns the next (>=) multiple of **R::min_size()**. If R is bound, returns **R::min_size()** if size is less than it. Otherwise returns size unchanged.

---

### resource_traits::guaranteed_alignment
<sub>Defined in header [&lt;memaw/resource_traits.hpp&gt;](/include/memaw/resource_traits.hpp)</sub>
```c++
constexpr static pow2_t guaranteed_alignment() noexcept;
```
Gets the minimum alignment of every allocation by the resource.

---

### resource_traits::min_size
<sub>Defined in header [&lt;memaw/resource_traits.hpp&gt;](/include/memaw/resource_traits.hpp)</sub>
```c++
constexpr static size_t min_size() noexcept;
```
Gets the minimum allocation size limit for the resource.

---

### allocate
<sub>Defined in header [&lt;memaw/resource_traits.hpp&gt;](/include/memaw/resource_traits.hpp)</sub>
```c++
template <exceptions_policy _policy = exceptions_policy::original, resource R>
[[nodiscard]] inline void* allocate
  (R& resource, size_t size, size_t alignment = alignof(std::max_align_t))
  noexcept(__detail::is_nothrow_with<_policy, __detail::has_nothrow_allocate<R>>);
```
Allocates memory from the given resource with the chosen exception policy. The parameters are forwarded to the **R::allocate()** call directly.

---

### allocate_at_least
<sub>Defined in header [&lt;memaw/resource_traits.hpp&gt;](/include/memaw/resource_traits.hpp)</sub>
```c++
template <exceptions_policy _policy = exceptions_policy::original, resource R>
[[nodiscard]] inline allocation_result allocate_at_least
  (R& resource, size_t size, size_t alignment = alignof(std::max_align_t))
  noexcept(__detail::is_nothrow_with<_policy, __detail::has_nothrow_allocate<R>>);
```
Allocates memory from the given resource with the chosen exception policy, while increasing the requested size if necessary (using **resource_traits::**[**ceil_allocation_size()**](#resource_traitsceil_allocation_size)).

---

### deallocate
<sub>Defined in header [&lt;memaw/resource_traits.hpp&gt;](/include/memaw/resource_traits.hpp)</sub>
```c++
template <exceptions_policy _policy = exceptions_policy::original, resource R>
  requires (_policy != exceptions_policy::throw_bad_alloc)
inline void deallocate(R& resource, void* ptr, size_t size,
                       size_t alignment = alignof(std::max_align_t))
  noexcept(__detail::is_nothrow_with<_policy, __detail::has_nothrow_deallocate<R>>);
```
Deallocates memory to the given resource with the chosen exception policy. The parameters are forwarded to the **R::deallocate()** call directly.

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

### enable_interchangeable_resources
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <resource R1, resource R2>
constexpr bool enable_interchangeable_resources = requires {
  { std::bool_constant<R1::template is_interchangeable_with<R2>>{} }
    -> std::same_as<std::true_type>;
```
A specializable global constant that enables the [**interchangeable_resource_with**](#interchangeable_resource_with) concept (see below) for R1 and R2 and vice versa. By default, true iff R1 defines a template constexpr member `R1::is_interchangeable_with<R2>` implicitly convertible to true.

---

### enable_substitutable_resource_for
<sub>Defined in header [&lt;memaw/concepts.hpp&gt;](/include/memaw/concepts.hpp)</sub>
```c++
template <resource R1, resource R2>
constexpr bool enable_substitutable_resource_for = requires {
  { std::bool_constant<R1::template is_substitutable_for<R2>>{} }
    -> std::same_as<std::true_type>;
```
A specializable global constant that enables the [**substitutable_resource_for**](#substitutable_resource_for) concept (see below) for R1 and R2. By default, true iff R1 defines a template constexpr member `R1::is_substitutable_for<R2>` implicitly convertible to true.

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
