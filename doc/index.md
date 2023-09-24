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
