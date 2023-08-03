#include "arena-container.h"

#include <gtest/gtest.h>

TEST(ArenaDeque, SimpleOps) {
  Arena a(1024);
  ArenaDeque<int, 11> container;

  // Make sure that multiple crossings of block-boundaries work well.
  for (int i = 0; i < 42; ++i) {
    container.Append(i, &a);
    int count = 0;
    for (int value : container) {
      EXPECT_EQ(value, count);
      count++;
    }

    for (int j = 0; j < i; ++j) {
      EXPECT_EQ(container[j], j);
    }
  }
}
