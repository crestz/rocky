#include <cstddef>
#include <gtest/gtest.h>

#include "hm_list.hxx"

using namespace rocky;

struct IntNode {
  int Val;
  std::atomic<std::uintptr_t> Next;
};

TEST(HMList, BasicAssertions) {
  //
  HMList<IntNode, &IntNode::Next> list;

  auto *node1 = new IntNode();
  node1->Val = 4;
  node1->Next = 0;

  // list.push(node1);

  // auto *node2 = list.Pop();

  // ASSERT_EQ(node1, node2);
  // ASSERT_EQ(node2->Val, node1->Val);
  // ASSERT_EQ(node2->Next, node1->Next);
}