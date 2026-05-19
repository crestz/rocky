#pragma once

#include <atomic>

#include "utils.hxx"

namespace rocky {

/*
  IntrusiveList

  Lock-free intrusive Treiber stack (LIFO).

  Requirements on T:
    - T must contain an intrusive next field:

        T *T::*Next

  Concurrency model:
    - Push() is lock-free.
    - Pop()  is lock-free.
    - Iteration is weakly consistent: safe only when no concurrent Pop() or
      Push() touches the region being traversed. For concurrent use, pair with
      an external reclamation protocol (EBR, hazard pointers).

  ABA hazard:
    Pop() is susceptible to ABA: if a popped node is freed and then reallocated
    at the same address before the CAS fires, the CAS may succeed on a stale
    successor. The surrounding reclamation protocol is responsible for delaying
    reuse until no thread holds a live reference.

  Memory reclamation:
    This implementation does NOT include a reclamation scheme. Callers are
    responsible for ensuring that popped nodes are not freed while any
    concurrent thread may still hold a pointer to them.
*/
template <typename T, T* T::* Next> class IntrusiveList {
public:
  // -- Iterators --------------------------------------------------------------

  class iterator {
  public:
    iterator() noexcept = default;
    explicit iterator(T* ptr) noexcept : curr{ptr} {}

    T* operator->() noexcept { return curr; }
    const T* operator->() const noexcept { return curr; }

    T& operator*() noexcept { return *curr; }
    const T& operator*() const noexcept { return *curr; }

    iterator& operator++() noexcept {
      if (curr != nullptr)
        curr = curr->*Next;
      return *this;
    }

    iterator operator++(int) noexcept {
      iterator old{*this};
      ++(*this);
      return old;
    }

    bool operator==(const iterator& other) const noexcept {
      return curr == other.curr;
    }
    bool operator!=(const iterator& other) const noexcept {
      return curr != other.curr;
    }

  private:
    T* curr{nullptr};
  };

  class const_iterator {
  public:
    const_iterator() noexcept = default;
    explicit const_iterator(const T* ptr) noexcept : curr{ptr} {}

    const T* operator->() const noexcept { return curr; }
    const T& operator*() const noexcept { return *curr; }

    const_iterator& operator++() noexcept {
      if (curr != nullptr)
        curr = curr->*Next;
      return *this;
    }

    const_iterator operator++(int) noexcept {
      const_iterator old{*this};
      ++(*this);
      return old;
    }

    bool operator==(const const_iterator& other) const noexcept {
      return curr == other.curr;
    }
    bool operator!=(const const_iterator& other) const noexcept {
      return curr != other.curr;
    }

  private:
    const T* curr{nullptr};
  };

  // -- Lifecycle --------------------------------------------------------------

  IntrusiveList() noexcept : head{nullptr} {}

  IntrusiveList(const IntrusiveList&) = delete;
  IntrusiveList& operator=(const IntrusiveList&) = delete;

  IntrusiveList(IntrusiveList&&) = delete;
  IntrusiveList& operator=(IntrusiveList&&) = delete;

  // -- Observation ------------------------------------------------------------

  /*
    Approximate emptiness check.

    Returns true if the head is currently nullptr.
    Concurrent Push()/Pop() may immediately change the result.
  */
  bool Empty() const noexcept {
    return head.load(std::memory_order_acquire) == nullptr;
  }

  iterator Begin() noexcept {
    return iterator{head.load(std::memory_order_acquire)};
  }
  iterator End() noexcept { return iterator{}; }

  const_iterator Begin() const noexcept {
    return const_iterator{head.load(std::memory_order_acquire)};
  }
  const_iterator End() const noexcept { return const_iterator{}; }

  /*
    Lowercase aliases for C++ range-for compatibility.
    Primary project-facing naming remains PascalCase: Begin()/End().
  */
  iterator begin() noexcept { return Begin(); }
  iterator end() noexcept { return End(); }

  const_iterator begin() const noexcept { return Begin(); }
  const_iterator end() const noexcept { return End(); }

  // -- Mutation ---------------------------------------------------------------

  /*
    Push node at the front.

    Preconditions:
      - node is not currently linked into any IntrusiveList.
      - caller is responsible for object lifetime.

    Note:
      Push() overwrites node->Next before publishing node, so a node returned
      from Pop() may be reused after the caller has established that no
      concurrent thread holds a reference to it (per the reclamation protocol).
  */
  void Push(T* node) noexcept {
    T* oldHead = head.load(std::memory_order_relaxed);
    node->*Next = oldHead;

    while (!head.compare_exchange_weak(oldHead, node, std::memory_order_release,
                                       std::memory_order_acquire)) {
      node->*Next = oldHead;
      CPURelax();
    }
  }

  /*
    Remove and return the front node.

    Returns:
      - the former head node, or nullptr if the list was empty.

    A successful Pop() grants exclusive ownership of the returned node's
    storage. The surrounding reclamation protocol determines when it is safe
    to free or reuse the node.
  */
  [[nodiscard]] T* Pop() noexcept {
    T* oldHead = head.load(std::memory_order_acquire);

    while (oldHead != nullptr) {
      T* newHead = oldHead->*Next;

      if (head.compare_exchange_weak(oldHead, newHead,
                                     std::memory_order_acq_rel,
                                     std::memory_order_acquire)) {
        return oldHead;
      }

      CPURelax();
    }

    return nullptr;
  }

private:
  std::atomic<T*> head;
};

} // namespace rocky
