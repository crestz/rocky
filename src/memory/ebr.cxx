#include "ebr_private.hxx"

#include <cassert>
#include <memory>
#include <utility>

namespace rocky::smr::ebr {
namespace detail {

Participant::Participant()
    : epoch{inactiveEpoch}, next{0}, threadNext{nullptr}, owner{nullptr},
      depth{0} {}

Cohort::Cohort() : epoch{0}, participants{} {}

Participant* Cohort::join() {
  thread_local ThreadState state;
  return state.join(this);
}

void Cohort::enroll(Participant* node) noexcept {
  assert(node != nullptr && "Cohort::enroll(): node is nil");
  participants.Push(node);
}

bool Cohort::unenroll(Participant* node) noexcept {
  assert(node != nullptr && "Cohort::unenroll(): node is nil");
  return participants.Remove(node);
}

void Cohort::enter(Participant* node) noexcept {
  assert(node != nullptr && "Cohort::enter(): participant node is nil");
  assert(node->depth >= 0 && "Cohort::enter(): node depth < 0");

  if (node->depth++ > 0)
    return;

  const auto currentEpoch = epoch.load(std::memory_order_seq_cst);

  node->epoch.store(currentEpoch, std::memory_order_release);
}

void Cohort::leave(Participant* node) noexcept {
  assert(node != nullptr && "Cohort::leave(): participant node is nil");
  assert(node->depth > 0 && "Cohort::leave(): unmatched leave");

  --node->depth;

  if (node->depth != 0)
    return;

  node->epoch.store(inactiveEpoch, std::memory_order_release);
}

ThreadState::ThreadState()
    : memPool{}, alloc{&memPool}, participants{nullptr} {}

Participant* ThreadState::join(Cohort* c) {
  assert(c != nullptr);

  for (auto* p = participants; p != nullptr; p = p->threadNext) {
    if (p->owner == c)
      return p;
  }

  auto* p = alloc.allocate(1);
  std::construct_at(p);

  p->owner = c;
  p->threadNext = participants;
  participants = p;

  c->enroll(p);

  return p;
}

ThreadState::~ThreadState() {
  for (auto* p = participants; p != nullptr;) {
    auto* next = p->threadNext;
    auto* c = p->owner;

    assert(c != nullptr);
    assert(p->depth == 0 &&
           "ThreadState::~ThreadState(): participant still active");

    p->epoch.store(inactiveEpoch, std::memory_order_release);

    assert(c->unenroll(p) && "ThreadState::~ThreadState(): failed to unenroll");

    std::destroy_at(p);

    // Optional. Since memPool is being destroyed, individual deallocation
    // is not required for storage reuse. But destroy_at is still correct.
    //
    // alloc.deallocate(p, 1);

    p = next;
  }
}

Cohort& defaultCohort() noexcept {
  static Cohort cohort;
  return cohort;
}

} // namespace detail

Guard::Guard() noexcept : cohort{nullptr}, participant{nullptr} {}

Guard::Guard(detail::Cohort* cohort, detail::Participant* participant) noexcept
    : cohort{cohort}, participant{participant} {}

Guard::Guard(Guard&& other) noexcept
    : cohort{std::exchange(other.cohort, nullptr)},
      participant{std::exchange(other.participant, nullptr)} {}

Guard& Guard::operator=(Guard&& other) noexcept {
  if (this == &other)
    return *this;

  ResetProtection();

  cohort = std::exchange(other.cohort, nullptr);
  participant = std::exchange(other.participant, nullptr);

  return *this;
}

Guard::~Guard() { ResetProtection(); }

bool Guard::Empty() const noexcept { return cohort == nullptr; }

void Guard::ResetProtection(std::nullptr_t) noexcept {
  if (cohort == nullptr)
    return;

  cohort->leave(participant);
  cohort = nullptr;
  participant = nullptr;
}

void Guard::Swap(Guard& other) noexcept {
  using std::swap;
  swap(cohort, other.cohort);
  swap(participant, other.participant);
}

Guard MakeGuard() {
  auto& cohort = detail::defaultCohort();
  auto* participant = cohort.join();
  cohort.enter(participant);

  return Guard{&cohort, participant};
}

void Swap(Guard& a, Guard& b) noexcept { a.Swap(b); }

} // namespace rocky::smr::ebr
