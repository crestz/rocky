#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "hm_list.hxx"

using namespace rocky;

// ──────────────────────────────────────────────────────────────────────────────
// Test node
// ──────────────────────────────────────────────────────────────────────────────

struct Node {
  int Val{0};
  std::atomic<std::uintptr_t> Next{0};

  Node() = default;
  explicit Node(int v) : Val{v} {}
};

using List = HMList<Node, &Node::Next>;

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

static int CountNodes(List &list) {
  int n = 0;
  for ([[maybe_unused]] auto &_ : list)
    ++n;
  return n;
}

static int CountNodesConst(const List &list) {
  int n = 0;
  for ([[maybe_unused]] const auto &_ : list)
    ++n;
  return n;
}

static std::vector<int> Vals(List &list) {
  std::vector<int> v;
  for (auto &n : list)
    v.push_back(n.Val);
  return v;
}

// ──────────────────────────────────────────────────────────────────────────────
// Fixture – owns heap nodes so tests stay leak-free
// ──────────────────────────────────────────────────────────────────────────────

class HMListTest : public ::testing::Test {
protected:
  List list;
  std::vector<Node *> owned;

  Node *make(int val = 0) {
    auto *n = new Node(val);
    owned.push_back(n);
    return n;
  }

  void TearDown() override {
    for (auto *n : owned)
      delete n;
  }
};

// ══════════════════════════════════════════════════════════════════════════════
// 1. Construction / Empty
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(HMListTest, DefaultConstructedIsEmpty) { EXPECT_TRUE(list.Empty()); }

TEST_F(HMListTest, EmptyFalseAfterPush) {
  list.Push(make(1));
  EXPECT_FALSE(list.Empty());
}

TEST_F(HMListTest, EmptyTrueAfterPushThenRemove) {
  auto *n = make(1);
  list.Push(n);
  EXPECT_TRUE(list.Remove(n));
  EXPECT_TRUE(list.Empty());
}

// ══════════════════════════════════════════════════════════════════════════════
// 2. Push
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(HMListTest, PushSingleNodeVisible) {
  auto *n = make(42);
  list.Push(n);
  EXPECT_TRUE(list.Contains(n));
  EXPECT_EQ(CountNodes(list), 1);
}

TEST_F(HMListTest, PushMultipleAllVisible) {
  auto *a = make(1), *b = make(2), *c = make(3);
  list.Push(a);
  list.Push(b);
  list.Push(c);

  EXPECT_TRUE(list.Contains(a));
  EXPECT_TRUE(list.Contains(b));
  EXPECT_TRUE(list.Contains(c));
}

TEST_F(HMListTest, PushIsLIFO) {
  // Push 1 then 2 then 3 → iteration order: 3, 2, 1
  auto *a = make(1), *b = make(2), *c = make(3);
  list.Push(a);
  list.Push(b);
  list.Push(c);

  auto v = Vals(list);
  ASSERT_EQ(v.size(), 3u);
  EXPECT_EQ(v[0], 3);
  EXPECT_EQ(v[1], 2);
  EXPECT_EQ(v[2], 1);
}

TEST_F(HMListTest, PushManyCountMatchesIteration) {
  static constexpr int N = 64;
  for (int i = 0; i < N; ++i)
    list.Push(make(i));
  EXPECT_EQ(CountNodes(list), N);
}

TEST_F(HMListTest, NodeCanBeReusedAfterRemove) {
  // Push() overwrites Next, so reuse after Remove() is safe
  // as long as the caller ensures no other thread still holds a reference.
  auto *n = make(1);
  list.Push(n);
  EXPECT_TRUE(list.Remove(n));

  n->Val = 99;
  list.Push(n); // Push rewrites n->Next
  EXPECT_TRUE(list.Contains(n));
  EXPECT_EQ(n->Val, 99);
}

// ══════════════════════════════════════════════════════════════════════════════
// 3. Remove
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(HMListTest, RemoveNullptrReturnsFalse) {
  EXPECT_FALSE(list.Remove(nullptr));
}

TEST_F(HMListTest, RemoveDummyReturnsFalse) {
  EXPECT_FALSE(list.Remove(list.Dummy()));
}

TEST_F(HMListTest, RemoveFromEmptyListReturnsFalse) {
  EXPECT_FALSE(list.Remove(make(1)));
}

TEST_F(HMListTest, RemoveAbsentNodeReturnsFalse) {
  list.Push(make(1));
  EXPECT_FALSE(list.Remove(make(99)));
}

TEST_F(HMListTest, RemoveOnlyNodeReturnsTrue) {
  auto *n = make(1);
  list.Push(n);
  EXPECT_TRUE(list.Remove(n));
}

TEST_F(HMListTest, RemoveOnlyNodeMakesListEmpty) {
  auto *n = make(1);
  list.Push(n);
  EXPECT_TRUE(list.Remove(n));
  EXPECT_TRUE(list.Empty());
}

TEST_F(HMListTest, RemoveHeadNode) {
  // After Push(a), Push(b), Push(c): order is c → b → a; c is head
  auto *a = make(1), *b = make(2), *c = make(3);
  list.Push(a);
  list.Push(b);
  list.Push(c);

  EXPECT_TRUE(list.Remove(c));
  EXPECT_FALSE(list.Contains(c));
  EXPECT_TRUE(list.Contains(a));
  EXPECT_TRUE(list.Contains(b));
  EXPECT_EQ(CountNodes(list), 2);
}

TEST_F(HMListTest, RemoveTailNode) {
  auto *a = make(1), *b = make(2), *c = make(3);
  list.Push(a);
  list.Push(b);
  list.Push(c);

  // Tail is a (first pushed)
  EXPECT_TRUE(list.Remove(a));
  EXPECT_FALSE(list.Contains(a));
  EXPECT_EQ(CountNodes(list), 2);
}

TEST_F(HMListTest, RemoveMiddleNode) {
  auto *a = make(1), *b = make(2), *c = make(3);
  list.Push(a);
  list.Push(b);
  list.Push(c);

  // Physical order: c → b → a; b is the middle node
  EXPECT_TRUE(list.Remove(b));
  EXPECT_FALSE(list.Contains(b));
  EXPECT_TRUE(list.Contains(a));
  EXPECT_TRUE(list.Contains(c));
  EXPECT_EQ(CountNodes(list), 2);
}

TEST_F(HMListTest, RemoveAlreadyRemovedReturnsFalse) {
  auto *n = make(1);
  list.Push(n);
  EXPECT_TRUE(list.Remove(n));
  EXPECT_FALSE(list.Remove(n)); // second call: already logically deleted
}

TEST_F(HMListTest, RemoveAllNodesOneByOne) {
  static constexpr int N = 10;
  std::vector<Node *> ns;
  for (int i = 0; i < N; ++i) {
    ns.push_back(make(i));
    list.Push(ns.back());
  }
  for (auto *n : ns) {
    EXPECT_TRUE(list.Remove(n));
  }
  EXPECT_TRUE(list.Empty());
  EXPECT_EQ(CountNodes(list), 0);
}

TEST_F(HMListTest, RemoveReducesIterationCount) {
  auto *a = make(1), *b = make(2), *c = make(3);
  list.Push(a);
  list.Push(b);
  list.Push(c);

  EXPECT_EQ(CountNodes(list), 3);
  EXPECT_TRUE(list.Remove(b));
  EXPECT_EQ(CountNodes(list), 2);
  EXPECT_TRUE(list.Remove(a));
  EXPECT_EQ(CountNodes(list), 1);
  EXPECT_TRUE(list.Remove(c));
  EXPECT_EQ(CountNodes(list), 0);
}

TEST_F(HMListTest, RemoveWithMultipleMarkedNeighbors) {
  // Tests that Remove() helps unlink already-marked nodes it encounters
  // while searching for target.
  auto *a = make(1), *b = make(2), *c = make(3), *d = make(4);
  list.Push(a);
  list.Push(b);
  list.Push(c);
  list.Push(d);

  // Order: d → c → b → a
  // Mark c and b via Remove, leaving d and a live.
  // Then remove a, which requires traversing past the marked c and b.
  EXPECT_TRUE(list.Remove(c));
  EXPECT_TRUE(list.Remove(b));
  EXPECT_TRUE(list.Remove(a));

  EXPECT_EQ(CountNodes(list), 1);
  EXPECT_TRUE(list.Contains(d));
}

// ══════════════════════════════════════════════════════════════════════════════
// 4. Contains
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(HMListTest, ContainsNullptrReturnsFalse) {
  EXPECT_FALSE(list.Contains(nullptr));
}

TEST_F(HMListTest, ContainsDummyReturnsFalse) {
  EXPECT_FALSE(list.Contains(list.Dummy()));
}

TEST_F(HMListTest, ContainsOnEmptyListReturnsFalse) {
  EXPECT_FALSE(list.Contains(make(1)));
}

TEST_F(HMListTest, ContainsPresentNodeReturnsTrue) {
  auto *n = make(42);
  list.Push(n);
  EXPECT_TRUE(list.Contains(n));
}

TEST_F(HMListTest, ContainsAbsentNodeReturnsFalse) {
  list.Push(make(1));
  EXPECT_FALSE(list.Contains(make(99)));
}

TEST_F(HMListTest, ContainsAfterRemoveReturnsFalse) {
  auto *n = make(1);
  list.Push(n);
  EXPECT_TRUE(list.Remove(n));
  EXPECT_FALSE(list.Contains(n));
}

TEST_F(HMListTest, ContainsTraversesPastDeletedNodes) {
  // Verifies Contains() walks the successor chain even when intermediate
  // nodes are logically deleted.
  auto *a = make(1), *b = make(2), *c = make(3);
  list.Push(a);
  list.Push(b);
  list.Push(c);

  // Physical order: c → b → a. Delete b, which sits between c and a.
  EXPECT_TRUE(list.Remove(b));

  EXPECT_TRUE(list.Contains(a)); // must traverse past the marked b
  EXPECT_TRUE(list.Contains(c));
  EXPECT_FALSE(list.Contains(b));
}

TEST_F(HMListTest, ContainsOnConstList) {
  auto *n = make(5);
  list.Push(n);
  const List &clist = list;
  EXPECT_TRUE(clist.Contains(n));
  EXPECT_FALSE(clist.Contains(make(99)));
  EXPECT_FALSE(clist.Contains(nullptr));
  EXPECT_FALSE(clist.Contains(clist.Dummy()));
}

// ══════════════════════════════════════════════════════════════════════════════
// 5. iterator
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(HMListTest, BeginEqualsEndOnEmptyList) {
  EXPECT_EQ(list.Begin(), list.End());
  EXPECT_EQ(list.begin(), list.end());
}

TEST_F(HMListTest, BeginNotEqualsEndOnNonEmptyList) {
  list.Push(make(1));
  EXPECT_NE(list.Begin(), list.End());
}

TEST_F(HMListTest, IteratorDereferenceStarAndArrow) {
  auto *n = make(99);
  list.Push(n);

  auto it = list.Begin();
  EXPECT_EQ(it->Val, 99);
  EXPECT_EQ((*it).Val, 99);
  EXPECT_EQ(&(*it), n);
}

TEST_F(HMListTest, IteratorPreIncrement) {
  auto *a = make(1), *b = make(2);
  list.Push(a);
  list.Push(b);

  // Order: b → a
  auto it = list.Begin();
  EXPECT_EQ(&(*it), b);
  ++it;
  EXPECT_EQ(&(*it), a);
  ++it;
  EXPECT_EQ(it, list.End());
}

TEST_F(HMListTest, IteratorPostIncrement) {
  auto *a = make(1), *b = make(2);
  list.Push(a);
  list.Push(b);

  auto it = list.Begin();
  auto prev = it++;
  EXPECT_EQ(&(*prev), b);
  EXPECT_EQ(&(*it), a);
}

TEST_F(HMListTest, IteratorOnEndIsNoop) {
  auto it = list.End();
  ++it;
  EXPECT_EQ(it, list.End());
}

TEST_F(HMListTest, IteratorEqualityAndInequality) {
  auto *n = make(1);
  list.Push(n);

  auto it1 = list.Begin();
  auto it2 = list.Begin();
  EXPECT_EQ(it1, it2);

  ++it1;
  EXPECT_NE(it1, it2);
  EXPECT_EQ(it1, list.End());
}

TEST_F(HMListTest, RangeForLoop) {
  list.Push(make(1));
  list.Push(make(2));
  list.Push(make(3));

  std::vector<int> got;
  for (auto &n : list)
    got.push_back(n.Val);

  ASSERT_EQ(got.size(), 3u);
  EXPECT_EQ(got[0], 3);
  EXPECT_EQ(got[1], 2);
  EXPECT_EQ(got[2], 1);
}

TEST_F(HMListTest, IteratorSkipsLogicallyDeletedHead) {
  auto *a = make(1), *b = make(2);
  list.Push(a);
  list.Push(b); // head = b

  EXPECT_TRUE(list.Remove(b)); // head is now deleted

  auto v = Vals(list);
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], 1);
}

TEST_F(HMListTest, IteratorSkipsDeletedMiddleNodes) {
  auto *a = make(1), *b = make(2), *c = make(3);
  list.Push(a);
  list.Push(b);
  list.Push(c);

  EXPECT_TRUE(list.Remove(b));

  auto v = Vals(list);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], 3);
  EXPECT_EQ(v[1], 1);
}

TEST_F(HMListTest, IteratorSkipsMultipleConsecutiveDeletedNodes) {
  static constexpr int N = 8;
  std::vector<Node *> ns;
  for (int i = 0; i < N; ++i) {
    ns.push_back(make(i));
    list.Push(ns.back());
  }
  // Delete all even-indexed (every other node)
  for (int i = 0; i < N; i += 2)
    EXPECT_TRUE(list.Remove(ns[i]));

  EXPECT_EQ(CountNodes(list), N / 2);
}

// ══════════════════════════════════════════════════════════════════════════════
// 6. const_iterator
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(HMListTest, ConstBeginEqualsEndOnEmptyList) {
  const List &c = list;
  EXPECT_EQ(c.Begin(), c.End());
  EXPECT_EQ(c.begin(), c.end());
}

TEST_F(HMListTest, ConstIteratorCoversAllNodes) {
  list.Push(make(1));
  list.Push(make(2));
  list.Push(make(3));
  EXPECT_EQ(CountNodesConst(list), 3);
}

TEST_F(HMListTest, ConstIteratorDereferenceStarAndArrow) {
  auto *n = make(77);
  list.Push(n);

  const List &c = list;
  auto it = c.begin();
  EXPECT_EQ(it->Val, 77);
  EXPECT_EQ((*it).Val, 77);
}

TEST_F(HMListTest, ConstIteratorPreIncrement) {
  auto *a = make(1), *b = make(2);
  list.Push(a);
  list.Push(b);

  const List &c = list;
  auto it = c.begin();
  EXPECT_EQ(it->Val, 2);
  ++it;
  EXPECT_EQ(it->Val, 1);
  ++it;
  EXPECT_EQ(it, c.end());
}

TEST_F(HMListTest, ConstIteratorPostIncrement) {
  list.Push(make(1));
  list.Push(make(2));

  const List &c = list;
  auto it = c.begin();
  auto old = it++;
  EXPECT_NE(it, old);
  EXPECT_EQ(it->Val, 1);
  EXPECT_EQ(old->Val, 2);
}

TEST_F(HMListTest, ConstIteratorOnEndIsNoop) {
  const List &c = list;
  auto it = c.end();
  ++it;
  EXPECT_EQ(it, c.end());
}

TEST_F(HMListTest, ConstRangeForLoop) {
  list.Push(make(10));
  list.Push(make(20));

  const List &c = list;
  std::vector<int> got;
  for (const auto &n : c)
    got.push_back(n.Val);

  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0], 20);
  EXPECT_EQ(got[1], 10);
}

TEST_F(HMListTest, ConstIteratorSkipsMarkedNodes) {
  auto *a = make(1), *b = make(2), *c = make(3);
  list.Push(a);
  list.Push(b);
  list.Push(c);
  EXPECT_TRUE(list.Remove(b));

  EXPECT_EQ(CountNodesConst(list), 2);
}

TEST_F(HMListTest, ConstIteratorEqualityAndInequality) {
  list.Push(make(5));
  const List &c = list;

  auto it1 = c.begin();
  auto it2 = c.begin();
  EXPECT_EQ(it1, it2);

  ++it1;
  EXPECT_NE(it1, it2);
  EXPECT_EQ(it1, c.end());
}

// ══════════════════════════════════════════════════════════════════════════════
// 7. Dummy()
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(HMListTest, DummyIsNonNull) { EXPECT_NE(list.Dummy(), nullptr); }

TEST_F(HMListTest, ConstDummyIsNonNull) {
  const List &c = list;
  EXPECT_NE(c.Dummy(), nullptr);
}

TEST_F(HMListTest, DummyNotVisibleViaContains) {
  EXPECT_FALSE(list.Contains(list.Dummy()));
}

TEST_F(HMListTest, DummyCannotBeRemoved) {
  EXPECT_FALSE(list.Remove(list.Dummy()));
}

TEST_F(HMListTest, DummyStableAcrossOperations) {
  Node *d = list.Dummy();
  list.Push(make(1));
  list.Push(make(2));
  EXPECT_EQ(list.Dummy(), d); // sentinel pointer is stable
}

// ══════════════════════════════════════════════════════════════════════════════
// 8. UnsafeSpliceFront (inline friend – callable via ADL in the rocky
// namespace)
// ══════════════════════════════════════════════════════════════════════════════

TEST_F(HMListTest, UnsafeSpliceFrontMovesAllNodes) {
  List src;
  auto *a = make(1), *b = make(2), *c = make(3);
  src.Push(a);
  src.Push(b);
  src.Push(c);

  UnsafeSpliceFront(list, src); // dst = list, src = src

  EXPECT_TRUE(src.Empty());
  EXPECT_EQ(CountNodes(list), 3);
  EXPECT_TRUE(list.Contains(a));
  EXPECT_TRUE(list.Contains(b));
  EXPECT_TRUE(list.Contains(c));
}

TEST_F(HMListTest, UnsafeSpliceFrontFromEmptySourceLeavesDestUnchanged) {
  List src;
  auto *n = make(1);
  list.Push(n);

  UnsafeSpliceFront(list, src);

  EXPECT_EQ(CountNodes(list), 1);
  EXPECT_TRUE(list.Contains(n));
}

TEST_F(HMListTest, UnsafeSpliceFrontIntoExistingDest) {
  List src;
  auto *a = make(1), *b = make(2);
  list.Push(a);
  src.Push(b);

  UnsafeSpliceFront(list, src);

  EXPECT_TRUE(src.Empty());
  EXPECT_EQ(CountNodes(list), 2);
  EXPECT_TRUE(list.Contains(a));
  EXPECT_TRUE(list.Contains(b));
}

TEST_F(HMListTest, UnsafeSpliceFrontSelfIsNoop) {
  list.Push(make(1));
  list.Push(make(2));
  UnsafeSpliceFront(list, list); // &dst == &src branch
  EXPECT_EQ(CountNodes(list), 2);
}

// ══════════════════════════════════════════════════════════════════════════════
// 9. Concurrent
//
// These tests are primarily correctness checks under race; they are most
// valuable when run under ThreadSanitizer (-fsanitize=thread).
// ══════════════════════════════════════════════════════════════════════════════

TEST(HMListConcurrent, ConcurrentPushAllNodesVisible) {
  static constexpr int T = 8, N = 128;
  List list;
  std::vector<Node> nodes(T * N);

  std::vector<std::thread> threads;
  threads.reserve(T);

  for (int t = 0; t < T; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < N; ++i) {
        nodes[t * N + i].Val = t * N + i;
        list.Push(&nodes[t * N + i]);
      }
    });
  }
  for (auto &th : threads)
    th.join();

  int count = 0;
  for ([[maybe_unused]] auto &_ : list)
    ++count;
  EXPECT_EQ(count, T * N);
}

TEST(HMListConcurrent, ConcurrentRemoveEachNodeExactlyOnce) {
  static constexpr int T = 8, N = 256;
  List list;
  std::vector<Node> nodes(N);

  for (int i = 0; i < N; ++i) {
    nodes[i].Val = i;
    list.Push(&nodes[i]);
  }

  std::atomic<int> removed{0};
  std::vector<std::thread> threads;
  threads.reserve(T);

  for (int t = 0; t < T; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = t; i < N; i += T) {
        if (list.Remove(&nodes[i])) {
          removed.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  for (auto &th : threads)
    th.join();

  // Every node is owned by exactly one thread: removed count == N.
  EXPECT_EQ(removed.load(), N);
  EXPECT_TRUE(list.Empty());
}

TEST(HMListConcurrent, ConcurrentPushAndRemoveNoDataRace) {
  static constexpr int T = 4, N = 64;
  List list;
  std::vector<Node> nodes(T * N);

  std::vector<std::thread> pushers, removers;
  pushers.reserve(T);
  removers.reserve(T);

  for (int t = 0; t < T; ++t) {
    pushers.emplace_back([&, t]() {
      for (int i = 0; i < N; ++i) {
        nodes[t * N + i].Val = t * N + i;
        list.Push(&nodes[t * N + i]);
      }
    });
  }

  for (int t = 0; t < T; ++t) {
    removers.emplace_back([&, t]() {
      for (int i = 0; i < N; ++i) {
        (void)list.Remove(
            &nodes[t * N + i]); // intentionally discarded: may return false
      }
    });
  }

  for (auto &th : pushers)
    th.join();
  for (auto &th : removers)
    th.join();

  int count = 0;
  for ([[maybe_unused]] auto &_ : list)
    ++count;
  EXPECT_GE(count, 0);
  EXPECT_LE(count, T * N);
}

TEST(HMListConcurrent, ConcurrentContainsWhilePushing) {
  static constexpr int N = 128;
  List list;
  std::vector<Node> nodes(N);
  std::atomic<bool> done{false};

  std::thread pusher([&]() {
    for (int i = 0; i < N; ++i) {
      nodes[i].Val = i;
      list.Push(&nodes[i]);
    }
    done.store(true, std::memory_order_release);
  });

  std::thread checker([&]() {
    while (!done.load(std::memory_order_acquire)) {
      for (int i = 0; i < N; ++i) {
        if (list.Contains(&nodes[i])) {
          EXPECT_EQ(nodes[i].Val, i); // value must be consistent
        }
      }
    }
  });

  pusher.join();
  checker.join();
}

TEST(HMListConcurrent, ConcurrentIterationWhileRemoving) {
  // Exercises advanceToLive() helping under live contention.
  static constexpr int N = 128;
  List list;
  std::vector<Node> nodes(N);

  for (int i = 0; i < N; ++i) {
    nodes[i].Val = i;
    list.Push(&nodes[i]);
  }

  std::atomic<bool> done{false};

  std::thread remover([&]() {
    for (int i = 0; i < N; i += 2) {
      (void)list.Remove(
          &nodes[i]); // intentionally discarded: racing with walker
    }
    done.store(true, std::memory_order_release);
  });

  // Walker exercises iterator helping of marked nodes under concurrent removal.
  std::thread walker([&]() {
    while (!done.load(std::memory_order_acquire)) {
      int count = 0;
      for ([[maybe_unused]] auto &_ : list)
        ++count;
      EXPECT_GE(count, 0);
      EXPECT_LE(count, N);
    }
  });

  remover.join();
  walker.join();
}

TEST(HMListConcurrent, ConcurrentContainsWhileRemoving) {
  static constexpr int N = 128;
  List list;
  std::vector<Node> nodes(N);

  for (int i = 0; i < N; ++i) {
    nodes[i].Val = i;
    list.Push(&nodes[i]);
  }

  std::atomic<bool> done{false};

  std::thread remover([&]() {
    for (int i = 0; i < N; ++i) {
      (void)list.Remove(
          &nodes[i]); // intentionally discarded: racing with checker
    }
    done.store(true, std::memory_order_release);
  });

  std::thread checker([&]() {
    while (!done.load(std::memory_order_acquire)) {
      for (int i = 0; i < N; ++i) {
        // Result is weakly consistent; we only check it doesn't crash.
        (void)list.Contains(&nodes[i]);
      }
    }
  });

  remover.join();
  checker.join();
}