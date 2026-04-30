#pragma once

#include <cassert>
#include <cstdint>

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#pragma intrinsic(_mm_pause)
#endif

namespace rocky {

using MarkedPtr = std::uintptr_t;

inline MarkedPtr Marked(std::uintptr_t raw) noexcept {
  std::uintptr_t p = reinterpret_cast<std::uintptr_t>(raw);
  assert((p & std::uintptr_t{1}) == 0 &&
         "raw pointer must have at least 2 byte alignment for marking.");
  return p | std::uintptr_t{1};
}

template <typename T> T *Unmarked(std::uintptr_t markedPtr) noexcept {
  return reinterpret_cast<T *>(markedPtr & ~std::uintptr_t{1});
}

inline bool IsMarked(std::uintptr_t p) noexcept {
  return (p & std::uintptr_t{1}) != 0;
}

inline void CPURelax() noexcept {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))

  _mm_pause();

#elif (defined(__GNUC__) || defined(__clang__)) &&                             \
    (defined(__i386__) || defined(__x86_64__))

  __builtin_ia32_pause();

#elif (defined(__GNUC__) || defined(__clang__)) &&                             \
    (defined(__aarch64__) || defined(__arm__))

  __asm__ __volatile__("yield" ::: "memory");

#elif (defined(__GNUC__) || defined(__clang__)) && defined(__riscv)

  __asm__ __volatile__("" ::: "memory");

#else

  // Last-resort compiler barrier, no headers required.
  __asm__ __volatile__("" ::: "memory");

#endif
}

} // namespace rocky