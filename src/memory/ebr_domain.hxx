#pragma once

#include <atomic>
#include <cstdint>
#include <memory_resource>

#include "intrusive_list.hxx"
#include "utils.hxx"

namespace rocky {

namespace smr::ebr {

constexpr auto InactiveEpoch = std::numeric_limits<std::uint64_t>::max();

struct Participant {
  std::atomic<std::uint64_t> PinnedEpoch;
  std::atomic<MarkedPtr> Next;
};

class Cohort {
public:
  void Enter();
  void Leave();

private:
  void publish(Participant *rec) noexcept;

  std::atomic<std::uint64_t> m_Epoch;
  std::atomic<Participant *> m_ThreadRecs;
};

constexpr std::size_t kThreadMaxCohorts = 16ULL;

struct ThreadCohortState {
  Cohort *Cohort;
  Participant Participant;
  ThreadCohortState *Next;
  ThreadCohortState *NextFree;
};

struct ThreadState {
  std::pmr::unsynchronized_pool_resource memory;
  IntrusiveList<ThreadCohortState, &ThreadCohortState::Next> cohorts;
  IntrusiveList<ThreadCohortState, &ThreadCohortState::NextFree> freeList;

  ThreadState() : memory{} {}

  ~ThreadState() {
    // for (std::uint32_t i = 0; i < cohortSize; ++i) {
    //   cohorts[i].Cohort->Leave();
    //   cohorts[i].Participant.Next.fetch_or(std::uintptr_t{1},
    //                                        std::memory_order_release);
    // }
  }
};

extern thread_local ThreadState tl_State;

} // namespace smr::ebr

} // namespace rocky