#pragma once

#include <atomic>

namespace rocky {

template <typename T, T *T::*Next> class IntrusiveList {
public:
  struct iterator {

    iterator() = default;
    explicit iterator(T *ptr) : m_Curr{ptr} {}

    const T *operator->() const { return m_Curr; }
    T *operator->() { return m_Curr; }

    const T &operator*() const { return *m_Curr; }
    T &operator*() { return *m_Curr; }

    iterator &operator++() noexcept {
      if (m_Curr != nullptr) {
        m_Curr = m_Curr->*Next;
      }
      return *this;
    }

    iterator operator++(int) noexcept {
      iterator old(*this);
      this->operator++();
      return old;
    }

    bool operator==(const iterator &ot) const noexcept {
      return m_Curr == ot.m_Curr;
    }
    bool operator!=(const iterator &ot) const noexcept {
      return m_Curr != ot.m_Curr;
    }

  private:
    T *m_Curr;
  };
  void Push(T *ptr) noexcept {
    T *oldHead = m_Head.load(std::memory_order_relaxed);
    ptr->*Next = oldHead;
    for (;
         !m_Head.compare_exchange_weak(oldHead, ptr, std::memory_order_acq_rel,
                                       std::memory_order_acquire);) {
      ptr->*Next = oldHead;
    }
  }

  T *Pop() noexcept {
    T *oldHead = m_Head.load(std::memory_order_acquire);
    for (; oldHead != nullptr;) {
      T *newHead = oldHead->*Next;
      if (m_Head.compare_exchange_weak(oldHead, newHead,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        break;
      }
    }
    return oldHead;
  }

  iterator begin() noexcept {
    return iterator(m_Head.load(std::memory_order_acquire));
  }
  const iterator begin() const noexcept {
    return iterator(m_Head.load(std::memory_order_acquire));
  }

  iterator end() noexcept { return iterator(nullptr); }
  const iterator end() const noexcept { return iterator(nullptr); }

private:
  std::atomic<T *> m_Head;
};

} // namespace rocky