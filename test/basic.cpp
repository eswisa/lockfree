#include "gtest/gtest.h"
#include "lockfree/lockfree.h"

class BasicTests : public ::testing::Test {
public:
  BasicTests() {
    m = new LockfreeMap(4);
  }

  ~BasicTests() {
    delete m;
  }

protected:
  LockfreeMap* m;
};

TEST_F(BasicTests, get_empty) {
  EXPECT_EQ(0, m -> get(1));
}

TEST_F(BasicTests,Insert_and_get) {
  m -> insert(1,1);

  EXPECT_EQ(1,m -> get(1));
}

TEST_F(BasicTests, Insert_and_get_another) {
  m -> insert(1,1);

  EXPECT_EQ(0, m -> get(2));
}

TEST_F(BasicTests, Insert_duplicate) {
  auto i1 = m -> insert(1,1);
  auto i2 = m -> insert(1,2);

  EXPECT_TRUE(i1);
  EXPECT_FALSE(i2);
  EXPECT_EQ(1, m -> get(1));
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

TEST_F(BasicTests, Insert_fails_when_map_is_full_and_remove_frees_space) {
  m -> insert(1, 11);
  m -> insert(2, 12);
  m -> insert(34, 44);
  m -> insert(5, 15);
  EXPECT_FALSE(m -> insert(6, 4));

  EXPECT_EQ(44, m -> remove(34));
  EXPECT_TRUE(m -> insert(6, 4));
}

// TEST_F(BasicTests, Get_fails_when_map_is_full) {
TEST_F(BasicTests, Get_fails_when_map_is_full_and_item_doesnt_exist) {
  m -> insert(1, 11);
  m -> insert(2, 12);
  m -> insert(34, 44);
  m -> insert(5, 15);
  EXPECT_EQ(0, m -> get(6));
}

// auto-growth tests
