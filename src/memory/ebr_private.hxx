#pragma once

#include "ebr.hxx"
#include "hm_list.hxx"

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory_resource>

namespace rocky::smr::ebr {
namespace detail {

constexpr std::uint64_t inactiveEpoch =
    std::numeric_limits<std::uint64_t>::max();

struct Participant {
  std::atomic<std::uint64_t> epoch;
  std::atomic<std::uintptr_t> next;
  Participant* threadNext;
  Cohort* owner;
  int depth;

  Participant();
};

class Cohort {
public:
  Cohort();
  ~Cohort() = default;

  Cohort(const Cohort&) = delete;
  Cohort& operator=(const Cohort&) = delete;

  Cohort(Cohort&&) = delete;
  Cohort& operator=(Cohort&&) = delete;

  Participant* join();
  void enroll(Participant* node) noexcept;
  bool unenroll(Participant* node) noexcept;
  void enter(Participant* node) noexcept;
  void leave(Participant* node) noexcept;

private:
  std::atomic<std::uint64_t> epoch;
  HMList<Participant, &Participant::next> participants;
};

struct ThreadState {
  std::pmr::unsynchronized_pool_resource memPool;
  std::pmr::polymorphic_allocator<Participant> alloc;
  Participant* participants;

  ThreadState();
  Participant* join(Cohort* c);
  ~ThreadState();
};

} // namespace detail
} // namespace rocky::smr::ebr
