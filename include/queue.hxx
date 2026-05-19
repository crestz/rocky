#pragma once

#include <array>
#include <new>
#include <utility>

namespace rocky {

template <typename T, size_t N = 8u> class CRQ {
public:
  bool Enqueue(T item) {
    uint64_t t = tail.fetch_add(1, std::memory_order_relaxed);

    if (closed.load(std::memory_order_relaxed))
      return false;

    auto cycle = t / N;
    auto idx = cacheRemap(t % N);
    auto& cell = buffer[idx];
    auto [safe, epoch] =
        unpack(cell.safeAndEpoch.load(std::memory_order_relaxed));
    auto value = cell.value.load(std::memory_order_relaxed);
    
  }

private:
  static constexpr size_t kCacheLineSize =
      std::hardware_destructive_interference_size;
  static constexpr std::uint64_t safeMask = 1ULL << 63;
  static constexpr std::uint64_t safeBit = 63;

  struct Slot {
    T data;
    std::atomic<std::uint64_t> safeAndEpoch;
  };

  static constexpr size_t kSlotsPerLine =
      (sizeof(Slot) >= kCacheLineSize) ? 1 // each slot already spans ≥1 line
                                       : (kCacheLineSize / sizeof(Slot));
  static constexpr size_t kNumLines = (N + kSlotsPerLine - 1) / kSlotsPerLine;

  static constexpr size_t kSlotBits =
      std::countr_zero(kSlotsPerLine); // log2(kSlotsPerLine)
  static constexpr size_t kLineBits =
      std::countr_zero(kNumLines); // log2(kNumLines)
  // kLineBits + kSlotBits == log2(N) — sanity check

  static constexpr size_t cacheRemap(size_t idx) noexcept {
    if constexpr (kSlotsPerLine == 1)
      return idx;
    const size_t whichLine = idx & (kNumLines - 1); // idx % kNumLines
    const size_t lineOffset = idx >> kLineBits;     // idx / kNumLines
    return (whichLine << kSlotBits) | lineOffset;
  }

  static thread_local std::uint64_t token;

  std::uint64_t pack(bool safe, std::uint64_t epoch) noexcept {
    return safe ? (safeMask | epoch) : (~safeMask & epoch);
  }

  std::pair<bool, std::uint64_t> unpack(std::uint64_t sne) noexcept {
    bool safe = ((safeMask & sne) >> safeBit) == 1;
    std::uint64_t epoch = ~safeMask & sne;
    return std::make_pair(safe, epoch);
  }

  std::array<Slot, N> buffer;
  alignas(kCacheLineSize) std::atomic<std::uint64_t> head;
  alignas(kCacheLineSize) std::atomic<std::uint64_t> tail;
  alignas(kCacheLineSize) std::atomic<bool> closed;
};

template <typename T> class MPMCQueue {
public:
private:
  static constexpr size_t ringSize = 8u;
  using RingBuffer = CRQ<T, ringSize>;
  struct Segment : private RingBuffer {
    std::atomic<Segment*> next;
    std::atomic<bool> closed;
  };

  std::atomic<Segment*> head;
  std::atomic<Segment*> tail;
};

} // namespace rocky