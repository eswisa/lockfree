#include "gtest/gtest.h"
#include "lockfree/lockfree.h"

class BasicTests : public ::testing::Test {
public:
  BasicTests() {
    m = new LockfreeMap(8);
  }

  ~BasicTests() {
    delete m;
  }

protected:
  LockfreeMap* m;
};

TEST_F(BasicTests,Insert_and_get) {
  m -> insert(1,1);

  EXPECT_EQ(1,m -> get(1));
}

TEST_F(BasicTests, get_empty) {
  EXPECT_EQ(0, m -> get(1));
}

TEST_F(BasicTests, Insert_and_get_another) {
  m -> insert(1,1);

  EXPECT_EQ(0, m -> get(2));
}

TEST_F(BasicTests, Insert_duplicate) {
  auto i1 = m -> insert(1,1);
  auto i2 = m -> insert(1,2);

  EXPECT_TRUE(i1);
  EXPECT_TRUE(i2);
}

TEST_F(BasicTests, Insert_and_remove) {
  auto i1 = m -> insert(1,1);
  m -> remove(1);
  auto i2 = m -> insert(1,2);

  EXPECT_TRUE(i1);
  EXPECT_TRUE(i2);
}

TEST_F(BasicTests, Delete_on_empty) {
  EXPECT_EQ(0, m -> remove(1));
}

// index-safe tests
// auto-growth tests
// thread-safety tests
