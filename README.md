# rocky

A C++20 library of lock-free and wait-free concurrent primitives, with safe memory reclamation. Inspired by libraries like Folly, but built from scratch as a learning and reference implementation.

## Goals

- Lock-free and wait-free data structures with well-defined memory ordering
- Safe memory reclamation strategies (EBR, hazard pointers, …)
- Thread-sanitizer–clean implementations validated under concurrency
- Header-only where practical; no hidden allocations in the hot path

## What's here

### Lock-free data structures

| Type | Header | Description |
|------|--------|-------------|
| `HMList<T, Next>` | `hm_list.hxx` | Harris-Michael lock-free singly-linked list. Supports concurrent `Push`, `Remove`, and `Contains`. Logical deletion is encoded in the low bit of the next pointer; physical unlinking is done lazily during traversal. |
| `IntrusiveList<T, Next>` | `intrusive_list.hxx` | Lock-free Treiber stack (LIFO). `Push` and `Pop` backed by a single CAS loop. |

### Memory reclamation (WIP)

| Type | Header | Description |
|------|--------|-------------|
| `EbrDomain` | `memory/ebr_domain.hxx` | Epoch-based reclamation domain. |

## Building

**Prerequisites:**
- CMake ≥ 4.0
- A C++20 compiler (Clang ≥ 14 or GCC ≥ 12)
- [vcpkg](https://github.com/microsoft/vcpkg) (for test dependencies)

```sh
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset ci        # debug + tests
cmake --build build/ci
```

For local development create a `CMakeUserPresets.json` (gitignored):

```json
{
  "version": 8,
  "configurePresets": [
    {
      "name": "debug",
      "inherits": "debug-base",
      "environment": { "VCPKG_ROOT": "/path/to/vcpkg" },
      "cacheVariables": { "ROCKY_BUILD_TESTS": true }
    }
  ]
}
```

## Running tests

```sh
ctest --test-dir build/ci --output-on-failure
```

All test targets are compiled with `-fsanitize=thread` to catch data races.

## Project layout

```
include/
  hm_list.hxx            Harris-Michael lock-free list
  intrusive_list.hxx     Lock-free Treiber stack
  utils.hxx              Shared utilities
src/
  hm_list_test.cc        Tests for HMList
  intrusive_list_test.cc Tests for IntrusiveList
  memory/
    ebr_domain.*         Epoch-based reclamation (WIP)
```
