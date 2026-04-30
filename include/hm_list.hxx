#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <type_traits>

#include "utils.hxx"

namespace rocky {

/*
  HMList

  Intrusive Harris-Michael-style singly-linked list.

  Requirements on T:
    - T must contain an intrusive next field:

        std::atomic<std::uintptr_t> T::*Next

    - The low bit of the stored pointer is used as the logical deletion mark.
    - T must be at least 2-byte aligned.
    - T must be default-constructible because this implementation stores an
      embedded dummy/sentinel node.

  Link encoding:
    - unmarked pointer: live edge to successor
    - marked pointer:   this node is logically deleted

  Dummy-head design:
    - m_DummyHead is never removed.
    - m_DummyHead.*Next is the actual list head.
    - There is no special "remove from head" case.

  Public concurrent API:
    Push(), Remove(), Contains(), Empty(), Begin(), End()

    All of these are safe to call concurrently with each other without
    external synchronization.

  Batch/unsafe API (private):
    UnsafeTakeAll(), UnsafePushChain()

    These require external exclusion against concurrent iteration, Remove(),
    Contains(), and other batch transfers on the same list. They are intended
    for use by EBR/hazard-pointer internals that own the list exclusively
    during the operation.

  Memory reclamation:
    This implementation does NOT include a reclamation scheme. Callers are
    responsible for ensuring that removed nodes are not freed while any
    concurrent thread may still hold a pointer into the list (e.g., via an
    iterator or an in-flight Remove()/Contains() traversal). Hazard pointers
    or epoch-based reclamation are suitable layered protocols.

  Concurrency model:
    - Push()     is lock-free.
    - Remove()   is lock-free under the usual Harris-Michael assumptions.
    - Contains() is lock-free.
    - Iteration  is weakly consistent and does not provide a snapshot.
*/
template <typename T, std::atomic<std::uintptr_t> T::*Next> class HMList {
  static_assert(alignof(T) >= 2,
                "HMList requires at least one spare low pointer bit");
  static_assert(std::is_default_constructible_v<T>,
                "HMList requires T to be default-constructible for dummy head");

  static constexpr std::uintptr_t MarkBit = std::uintptr_t{1};

public:
  class iterator {
  public:
    /*
      End iterator.

      Invariant:
        m_Curr == nullptr means end().
        m_Dummy is nullptr only for the default/end iterator; it is never
        accessed in that state.
    */
    iterator() noexcept : m_Dummy{nullptr}, m_Curr{nullptr}, m_Prev{nullptr} {}

    /*
      Begin iterator.

      Starts at dummy->Next and advances until either:
        - a live node is found, or
        - the end of the list is reached.

      The iterator may opportunistically help unlink marked nodes.
    */
    explicit iterator(T *dummy) noexcept
        : m_Dummy{dummy}, m_Curr{nullptr}, m_Prev{dummy} {
      m_Curr = loadUnmarkedNext(m_Dummy);
      advanceToLive();
    }

    const T *operator->() const noexcept { return m_Curr; }
    T *operator->() noexcept { return m_Curr; }

    const T &operator*() const noexcept { return *m_Curr; }
    T &operator*() noexcept { return *m_Curr; }

    /*
      Advance to the next live node.

      Iterator progress rule:
        - First move forward from the current node.
        - Then skip marked nodes.
        - Do not restart from the dummy head on failed helping CAS.

      This avoids the iterator jumping backward and repeatedly visiting the
      same prefix of the list under contention.

      Weak consistency:
        - Concurrent insertions may or may not be observed.
        - Concurrent deletions may cause nodes to be skipped.
        - The iterator does not provide a stable snapshot.
    */
    iterator &operator++() noexcept {
      if (m_Curr == nullptr) {
        return *this;
      }

      m_Prev = m_Curr;
      m_Curr = loadUnmarkedNext(m_Curr);

      advanceToLive();
      return *this;
    }

    iterator operator++(int) noexcept {
      iterator old{*this};
      ++(*this);
      return old;
    }

    bool operator==(const iterator &other) const noexcept {
      return m_Curr == other.m_Curr;
    }

    bool operator!=(const iterator &other) const noexcept {
      return !(*this == other);
    }

  private:
    static T *loadUnmarkedNext(T *node) noexcept {
      std::uintptr_t raw = (node->*Next).load(std::memory_order_acquire);
      return Unmarked<T>(raw);
    }

    /*
      Advance m_Curr until it is either nullptr or observed live.

      Iterator invariants:

        1. m_Dummy is the permanent sentinel node.
           It is never logically deleted and never physically removed.

        2. m_Curr is the candidate node to return.

        3. m_Prev is a best-effort predecessor of m_Curr along the path
           observed by this iterator.

        4. m_Prev is not guaranteed to still be reachable from m_Dummy.
           It may have become logically deleted concurrently.

        5. Because of invariant 4, helping from the iterator is opportunistic.
           A successful CAS helps unlink m_Curr.
           A failed CAS only means the observed predecessor/current relation
           is stale.

        6. On failed CAS, this iterator preserves forward movement by walking
           to succ rather than restarting from the head.

      Why this is acceptable:

        - The deletion mark belongs to the node's own Next field.
        - Returning a node requires observing its own Next field as unmarked.
        - The iterator is not the primary structural maintenance algorithm.
          Mutating operations use stronger validation/retry.
    */
    void advanceToLive() noexcept {
      while (m_Curr != nullptr) {
        std::uintptr_t currNext =
            (m_Curr->*Next).load(std::memory_order_acquire);

        T *succ = Unmarked<T>(currNext);

        if (!IsMarked(currNext)) {
          return;
        }

        /*
          m_Curr is logically deleted.

          Try to physically unlink:

              m_Prev -> m_Curr -> succ

          into:

              m_Prev -> succ

          This does not revive m_Curr. The logical deletion mark is stored in
          m_Curr->Next, and this CAS does not modify m_Curr->Next.
        */
        std::uintptr_t expected = reinterpret_cast<std::uintptr_t>(m_Curr);
        std::uintptr_t desired = reinterpret_cast<std::uintptr_t>(succ);

        if ((m_Prev->*Next)
                .compare_exchange_strong(expected, desired,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
          m_Curr = succ;
          continue;
        }

        /*
          Help failed.

          Do not restart. Preserve iterator progress by continuing forward
          along the successor chain that was already observed.
        */
        m_Prev = m_Curr;
        m_Curr = succ;
      }
    }

  private:
    T *m_Dummy;
    T *m_Curr;
    T *m_Prev;
  };

  /*
    const_iterator: read-only traversal.

    Identical traversal logic to iterator but does not attempt to physically
    unlink marked nodes, since that requires a mutable predecessor CAS.
    Logically deleted nodes are simply skipped by walking the successor chain.
  */
  class const_iterator {
  public:
    const_iterator() noexcept : m_Curr{nullptr} {}

    explicit const_iterator(const T *dummy) noexcept : m_Curr{nullptr} {
      std::uintptr_t raw = (dummy->*Next).load(std::memory_order_acquire);
      m_Curr = Unmarked<T>(raw);
      skipMarked();
    }

    const T *operator->() const noexcept { return m_Curr; }
    const T &operator*() const noexcept { return *m_Curr; }

    const_iterator &operator++() noexcept {
      if (m_Curr == nullptr) {
        return *this;
      }
      std::uintptr_t raw = (m_Curr->*Next).load(std::memory_order_acquire);
      m_Curr = Unmarked<T>(raw);
      skipMarked();
      return *this;
    }

    const_iterator operator++(int) noexcept {
      const_iterator old{*this};
      ++(*this);
      return old;
    }

    bool operator==(const const_iterator &other) const noexcept {
      return m_Curr == other.m_Curr;
    }

    bool operator!=(const const_iterator &other) const noexcept {
      return !(*this == other);
    }

  private:
    void skipMarked() noexcept {
      while (m_Curr != nullptr) {
        std::uintptr_t raw = (m_Curr->*Next).load(std::memory_order_acquire);
        if (!IsMarked(raw)) {
          return;
        }
        /*
          Node is logically deleted; walk forward without helping.
          Helping requires a mutable predecessor, which const_iterator
          does not track.
        */
        m_Curr = Unmarked<T>(raw);
      }
    }

    const T *m_Curr;
  };

public:
  HMList() noexcept : m_DummyHead{} {
    /*
      The dummy head is permanent.
      Its Next field is the actual list head and starts empty.
    */
    (m_DummyHead.*Next).store(0, std::memory_order_relaxed);
  }

  HMList(const HMList &) = delete;
  HMList &operator=(const HMList &) = delete;

  HMList(HMList &&) = delete;
  HMList &operator=(HMList &&) = delete;

  /*
    Approximate emptiness check.

    Returns true if the dummy head currently points to nullptr.
    Concurrent Push()/Remove() may immediately change the result.
  */
  bool Empty() const noexcept {
    std::uintptr_t raw = (m_DummyHead.*Next).load(std::memory_order_acquire);
    return Unmarked<T>(raw) == nullptr;
  }

  iterator Begin() noexcept { return iterator{&m_DummyHead}; }
  iterator End() noexcept { return iterator{}; }

  const_iterator Begin() const noexcept { return const_iterator{&m_DummyHead}; }
  const_iterator End() const noexcept { return const_iterator{}; }

  /*
    Lowercase aliases for C++ range-for compatibility.
    Primary project-facing naming remains Go-style: Begin()/End().
  */
  iterator begin() noexcept { return Begin(); }
  iterator end() noexcept { return End(); }

  const_iterator begin() const noexcept { return Begin(); }
  const_iterator end() const noexcept { return End(); }

  /*
    Push node at the front.

    Precondition:
      - node is not currently linked into any HMList.
      - caller is responsible for object lifetime discipline.
      - caller must not concurrently Push()/Remove() the same node elsewhere.

    Note:
      Push() overwrites node->Next before publishing node, so a node returned
      from Remove() may be reused after the caller knows it is safe to do so
      (subject to the surrounding reclamation protocol).
  */
  void Push(T *node) noexcept {
    auto &head = m_DummyHead.*Next;

    std::uintptr_t oldHead = head.load(std::memory_order_acquire);

    for (;;) {
      /*
        The dummy head's Next field is only ever written with unmarked
        addresses. Stripping the mark bit here is purely defensive.

        Publish the new node with an unmarked successor pointer so it
        is immediately visible as live to concurrent readers.

        The relaxed store to node->Next is safe: the subsequent release
        CAS on head creates a happens-before edge with any acquire load
        of head, making the relaxed store visible to any thread that
        then reads head.
      */
      (node->*Next).store(oldHead & ~MarkBit, std::memory_order_relaxed);

      std::uintptr_t desired = reinterpret_cast<std::uintptr_t>(node);

      if (head.compare_exchange_weak(oldHead, desired,
                                     std::memory_order_release,
                                     std::memory_order_acquire)) {
        return;
      }

      CPURelax();
    }
  }

  /*
    Remove target from the list.

    Returns:
      - true if this call logically removed target;
      - false if target was not found or was already logically deleted.

    Linearization point:
      - successful CAS that marks target->Next.

    Physical unlinking:
      - best effort after logical deletion succeeds.
      - other mutators/iterators may also help unlink marked nodes.

    Complexity:
      - O(n), because this unordered singly-linked list must search for target.
  */
  [[nodiscard]] bool Remove(T *target) noexcept {
    if (target == nullptr || target == &m_DummyHead) {
      return false;
    }

    for (;;) {
      T *pred = &m_DummyHead;
      T *curr = Unmarked<T>((pred->*Next).load(std::memory_order_acquire));

      while (curr != nullptr) {
        std::uintptr_t currNext = (curr->*Next).load(std::memory_order_acquire);

        T *succ = Unmarked<T>(currNext);

        if (IsMarked(currNext)) {
          /*
            curr is already logically deleted. Help unlink it.

            This mutating operation is allowed to restart on failed validation;
            unlike the iterator, operation-level algorithms prefer structural
            correctness over forward-only traversal.
          */
          std::uintptr_t expected = reinterpret_cast<std::uintptr_t>(curr);
          std::uintptr_t desired = reinterpret_cast<std::uintptr_t>(succ);

          if (!(pred->*Next)
                   .compare_exchange_strong(expected, desired,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            break; // predecessor changed; restart the search
          }

          curr = succ;
          continue;
        }

        if (curr == target) {
          /*
            Try to logically delete target by marking target->Next.

            We re-load target->Next explicitly here so that on CAS failure
            we have a freshly observed value to reason about, rather than
            relying on the value written back by compare_exchange_strong.
          */
          std::uintptr_t expected = currNext; // known unmarked at this point
          std::uintptr_t marked = currNext | MarkBit;

          if ((curr->*Next)
                  .compare_exchange_strong(expected, marked,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
            /*
              Logical deletion succeeded. Best-effort physical unlink:

                  pred -> target -> succ

              into:

                  pred -> succ

              Failure here is harmless; another thread will help eventually.
            */
            std::uintptr_t predExpected =
                reinterpret_cast<std::uintptr_t>(target);
            std::uintptr_t predDesired = reinterpret_cast<std::uintptr_t>(succ);

            (pred->*Next)
                .compare_exchange_strong(predExpected, predDesired,
                                         std::memory_order_acq_rel,
                                         std::memory_order_acquire);

            return true;
          }

          /*
            CAS failed. `expected` now holds the current value of target->Next
            as written back by compare_exchange_strong.

            If target is now marked, another thread already removed it.
            Otherwise the list structure changed under us; restart.
          */
          if (IsMarked(expected)) {
            return false;
          }

          break; // restart the search
        }

        pred = curr;
        curr = succ;
      }

      if (curr == nullptr) {
        return false;
      }

      CPURelax();
    }
  }

  /*
    Check whether target is currently reachable and observed live.

    This is a weakly consistent concurrent query:
      - true  means target was observed reachable and unmarked at some point;
      - false means target was not found, or was observed logically deleted.

    Marked (logically deleted) nodes are skipped during traversal; they do
    not cause Contains() to return true for themselves even when still
    physically reachable from the dummy head.
  */
  [[nodiscard]] bool Contains(const T *target) const noexcept {
    if (target == nullptr || target == &m_DummyHead) {
      return false;
    }

    const T *curr =
        Unmarked<T>((m_DummyHead.*Next).load(std::memory_order_acquire));

    while (curr != nullptr) {
      std::uintptr_t currNext = (curr->*Next).load(std::memory_order_acquire);

      if (curr == target) {
        return !IsMarked(currNext);
      }

      /*
        Strip the mark so we can continue traversal past logically deleted
        nodes. We do not help unlink them here because Contains() is const
        and helping requires a mutable predecessor CAS.
      */
      curr = Unmarked<T>(currNext);
    }

    return false;
  }

  /*
    Expose the permanent dummy node for algorithms that need to run a full
    Harris-Michael search from a known stable predecessor.

    Important constraints:
      - The returned pointer must never be passed to Remove().
      - The returned pointer must not be treated as a data node.
      - It is safe to use as a starting predecessor in a custom traversal.
  */
  T *Dummy() noexcept { return &m_DummyHead; }
  const T *Dummy() const noexcept { return &m_DummyHead; }

private:
  /*
    Atomically detach the currently visible head chain.

    Returns:
      - first node of the detached chain, or nullptr if empty.

    UNSAFE. Requires external exclusion against concurrent iteration,
    Remove(), Contains(), and other batch transfers on this list.

    Concurrent Push() is safe under these semantics:
      - pushes linearized before exchange are included in the returned chain;
      - pushes linearized after exchange remain in this list.

    The returned chain may contain nodes whose Next fields are marked if
    concurrent removals were active before exclusion was established.
    Caller owns the returned chain subject to the surrounding reclamation
    protocol.
  */
  [[nodiscard]] T *UnsafeTakeAll() noexcept {
    auto &head = m_DummyHead.*Next;
    std::uintptr_t old = head.exchange(0, std::memory_order_acq_rel);
    return Unmarked<T>(old);
  }

  /*
    Push a private chain to the front.

    Precondition:
      - first is either nullptr or points to a private, unshared chain.
      - every Next pointer in the chain is unmarked (live).
      - no node in the chain is concurrently reachable from another list.

    UNSAFE. Requires external exclusion equivalent to UnsafeTakeAll().

    This function asserts that chain nodes are unmarked rather than silently
    stripping marks, to catch misuse of logically deleted nodes during
    development.
  */
  void UnsafePushChain(T *first) noexcept {
    if (first == nullptr) {
      return;
    }

    T *tail = first;

    for (;;) {
      std::uintptr_t nextRaw = (tail->*Next).load(std::memory_order_relaxed);

      assert(!IsMarked(nextRaw) &&
             "UnsafePushChain: chain contains a logically deleted node");

      T *succ = Unmarked<T>(nextRaw);

      if (succ == nullptr) {
        break;
      }

      tail = succ;
    }

    auto &head = m_DummyHead.*Next;
    std::uintptr_t oldHead = head.load(std::memory_order_acquire);

    for (;;) {
      T *oldHeadPtr = Unmarked<T>(oldHead);

      (tail->*Next)
          .store(reinterpret_cast<std::uintptr_t>(oldHeadPtr),
                 std::memory_order_relaxed);

      std::uintptr_t desired = reinterpret_cast<std::uintptr_t>(first);

      if (head.compare_exchange_weak(oldHead, desired,
                                     std::memory_order_release,
                                     std::memory_order_acquire)) {
        return;
      }

      CPURelax();
    }
  }

  /*
    Move all currently visible nodes from src to the front of dst.

    UNSAFE. Requires external exclusion equivalent to UnsafeTakeAll() on src.
    See UnsafeTakeAll() and UnsafePushChain() for full preconditions.
  */
  friend void UnsafeSpliceFront(HMList &dst, HMList &src) noexcept {
    if (&dst == &src) {
      return;
    }

    T *chain = src.UnsafeTakeAll();

    if (chain != nullptr) {
      dst.UnsafePushChain(chain);
    }
  }

private:
  T m_DummyHead;
};

} // namespace rocky