#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "intrusive_list.hxx"

using namespace rocky;

struct Node {
  int Val{0};
  Node *Next{nullptr};

  Node() = default;
  explicit Node(int v) : Val{v} {}
};

using List = IntrusiveList<Node, &Node::Next>;

// ══════════════════════════════════════════════════════════════════════════════
// 1. Construction
// ══════════════════════════════════════════════════════════════════════════════

TEST(IntrusiveListTest, DefaultConstructedIsEmpty) {
  List list;
  EXPECT_EQ(list.begin(), list.end());
}

// ══════════════════════════════════════════════════════════════════════════════
// 2. Push
// ══════════════════════════════════════════════════════════════════════════════

TEST(IntrusiveListTest, PushSingleNodeVisible) {
  List list;
  Node n(42);
  list.Push(&n);
  ASSERT_NE(list.begin(), list.end());
  EXPECT_EQ(list.begin()->Val, 42);
}

TEST(IntrusiveListTest, PushIsLIFO) {
  List list;
  Node a(1), b(2), c(3);
  list.Push(&a);
  list.Push(&b);
  list.Push(&c);

  auto it = list.begin();
  EXPECT_EQ(it->Val, 3); ++it;
  EXPECT_EQ(it->Val, 2); ++it;
  EXPECT_EQ(it->Val, 1); ++it;
  EXPECT_EQ(it, list.end());
}

TEST(IntrusiveListTest, PushMultipleAllVisible) {
  List list;
  Node a(1), b(2), c(3);
  list.Push(&a);
  list.Push(&b);
  list.Push(&c);

  int count = 0;
  for ([[maybe_unused]] auto &_ : list)
    ++count;
  EXPECT_EQ(count, 3);
}

// ══════════════════════════════════════════════════════════════════════════════
// 3. Pop
// ══════════════════════════════════════════════════════════════════════════════

TEST(IntrusiveListTest, PopFromEmptyReturnsNull) {
  List list;
  EXPECT_EQ(list.Pop(), nullptr);
}

TEST(IntrusiveListTest, PopReturnsPushedNode) {
  List list;
  Node n(7);
  list.Push(&n);
  EXPECT_EQ(list.Pop(), &n);
}

TEST(IntrusiveListTest, PopIsLIFO) {
  List list;
  Node a(1), b(2), c(3);
  list.Push(&a);
  list.Push(&b);
  list.Push(&c);

  EXPECT_EQ(list.Pop(), &c);
  EXPECT_EQ(list.Pop(), &b);
  EXPECT_EQ(list.Pop(), &a);
  EXPECT_EQ(list.Pop(), nullptr);
}

TEST(IntrusiveListTest, PopMakesListEmpty) {
  List list;
  Node n(1);
  list.Push(&n);
  list.Pop();
  EXPECT_EQ(list.begin(), list.end());
}

TEST(IntrusiveListTest, PopAllNodes) {
  static constexpr int N = 16;
  List list;
  std::vector<Node> nodes(N);
  for (int i = 0; i < N; ++i) {
    nodes[i].Val = i;
    list.Push(&nodes[i]);
  }
  int count = 0;
  while (list.Pop() != nullptr)
    ++count;
  EXPECT_EQ(count, N);
  EXPECT_EQ(list.begin(), list.end());
}

// ══════════════════════════════════════════════════════════════════════════════
// 4. Iterator
// ══════════════════════════════════════════════════════════════════════════════

TEST(IntrusiveListTest, BeginEqualsEndOnEmptyList) {
  List list;
  EXPECT_EQ(list.begin(), list.end());
}

TEST(IntrusiveListTest, IteratorPreIncrement) {
  List list;
  Node a(1), b(2);
  list.Push(&a);
  list.Push(&b);

  auto it = list.begin();
  EXPECT_EQ(&(*it), &b); ++it;
  EXPECT_EQ(&(*it), &a); ++it;
  EXPECT_EQ(it, list.end());
}

TEST(IntrusiveListTest, IteratorPostIncrement) {
  List list;
  Node a(1), b(2);
  list.Push(&a);
  list.Push(&b);

  auto it = list.begin();
  auto prev = it++;
  EXPECT_EQ(&(*prev), &b);
  EXPECT_EQ(&(*it), &a);
}

TEST(IntrusiveListTest, IteratorDereferenceStarAndArrow) {
  List list;
  Node n(99);
  list.Push(&n);

  auto it = list.begin();
  EXPECT_EQ(it->Val, 99);
  EXPECT_EQ((*it).Val, 99);
  EXPECT_EQ(&(*it), &n);
}

TEST(IntrusiveListTest, RangeForLoop) {
  List list;
  Node a(1), b(2), c(3);
  list.Push(&a);
  list.Push(&b);
  list.Push(&c);

  std::vector<int> got;
  for (auto &node : list)
    got.push_back(node.Val);

  ASSERT_EQ(got.size(), 3u);
  EXPECT_EQ(got[0], 3);
  EXPECT_EQ(got[1], 2);
  EXPECT_EQ(got[2], 1);
}

TEST(IntrusiveListTest, NodeCanBeReusedAfterPop) {
  List list;
  Node n(1);
  list.Push(&n);
  list.Pop();
  n.Val = 42;
  n.Next = nullptr;
  list.Push(&n);
  ASSERT_NE(list.begin(), list.end());
  EXPECT_EQ(list.begin()->Val, 42);
}

// ══════════════════════════════════════════════════════════════════════════════
// 5. Concurrent
// ══════════════════════════════════════════════════════════════════════════════

TEST(IntrusiveListConcurrent, ConcurrentPushAllNodesVisible) {
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

TEST(IntrusiveListConcurrent, ConcurrentPushAndPopNoDataRace) {
  static constexpr int T = 4, N = 128;
  List list;
  std::vector<Node> nodes(T * N);

  std::vector<std::thread> pushers;
  pushers.reserve(T);
  for (int t = 0; t < T; ++t) {
    pushers.emplace_back([&, t]() {
      for (int i = 0; i < N; ++i) {
        nodes[t * N + i].Val = t * N + i;
        list.Push(&nodes[t * N + i]);
      }
    });
  }

  std::atomic<int> popped{0};
  std::vector<std::thread> poppers;
  poppers.reserve(T);
  for (int t = 0; t < T; ++t) {
    poppers.emplace_back([&]() {
      for (int i = 0; i < N; ++i) {
        if (list.Pop() != nullptr)
          popped.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  for (auto &th : pushers)
    th.join();
  for (auto &th : poppers)
    th.join();

  while (list.Pop() != nullptr)
    popped.fetch_add(1, std::memory_order_relaxed);

  EXPECT_EQ(popped.load(), T * N);
}
