#include "gtest/gtest.h"
#include "lockfree/lockfree.h"

TEST(LockfreeMap,Insert_and_get) {
  LockfreeMap m;
  m.insert(1,1);

  EXPECT_EQ(1,m.get(1));
}

TEST(LockfreeMap, get_empty) {
  LockfreeMap m;

  EXPECT_EQ(0, m.get(1));
}

TEST(LockfreeMap, Insert_and_get_another) {
  LockfreeMap m;

  m.insert(1,1);

  EXPECT_EQ(0, m.get(2));
}

TEST(LockfreeMap, Insert_duplicate) {
  LockfreeMap m;

  auto i1 = m.insert(1,1);
  auto i2 = m.insert(1,2);

  EXPECT_TRUE(i1);
  EXPECT_FALSE(i2);
}

TEST(LockfreeMap, Insert_and_remove) {
  LockfreeMap m;

  auto i1 = m.insert(1,1);
  m.remove(1);
  auto i2 = m.insert(1,2);

  EXPECT_TRUE(i1);
  EXPECT_TRUE(i2);
}

TEST(LockfreeMap, Delete_on_empty) {
  LockfreeMap m;

  EXPECT_EQ(0, m.remove(1));
}
