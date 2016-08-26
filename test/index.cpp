#include "gtest/gtest.h"
#include "lockfree/lockfree.h"

class IndexTests : public ::testing::Test {
public:
  IndexTests() {
    m = new LockFreeMap<int, int>(8);
  }

  ~IndexTests() {
    delete m;
  }

protected:
  LockFreeMap<int, int>* m;
};

TEST_F(IndexTests,Insert_and_get_indices_greater_than_size) {
  m -> insert(9,1);

  EXPECT_EQ(1,m -> get(9));
}
