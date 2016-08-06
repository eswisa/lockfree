#include "gtest/gtest.h"
#include "lockfree/lockfree.h"
#include <thread>
#include <atomic>
#include <functional>
#include <stdlib.h>

class ThreadSafetyTests : public ::testing::Test {
public:
  ThreadSafetyTests() {
    m = new LockfreeMap(size);
  }

  ~ThreadSafetyTests() {
    delete m;
  }

protected:
  LockfreeMap* m;
  int size = 32000;
};

void insertingThread(int number, LockfreeMap* m, std::atomic<int>* successfulInsertions) {
  auto successes = 0;
  auto base = number * number * 100000;
  for (int i = 1; i < 33000; ++i) {
    if (m -> insert(base + i, i)) {
      ++successes;
    }
  }

  printf("thread %d did %d insertions\n", number, successes);

  std::atomic_fetch_add(successfulInsertions, successes);
}

TEST_F(ThreadSafetyTests, count_number_of_succesful_insertions_keys_dont_collide_and_map_doesnt_grow) {
  std::atomic<int> successfulInsertions;

  std::atomic_init(&successfulInsertions, 0);

  std::thread t1 (insertingThread, 1, m, &successfulInsertions);
  std::thread t2 (insertingThread, 2, m, &successfulInsertions);
  std::thread t3 (insertingThread, 3, m, &successfulInsertions);

  t1.join();
  t2.join();
  t3.join();

  EXPECT_EQ(size, successfulInsertions);
}

template <int N = 32000>
void randomActionsThread(int number, LockfreeMap* m, std::atomic<int>* successfulInsertions, std::atomic<int>* successfulRemovals) {
  auto localSuccessfulInsertions = 0;
  auto localSuccessfulRemovals = 0;

  for (int i = 1; i <= 1000000; ++i) {
    auto randomKey = (rand() % N) + 1;
    auto decisionRoullete = i % 100 + 1; //1-100

    //remove
    if (decisionRoullete > 99) {
      auto value = m -> remove(randomKey);
      if (value != 0) {
        ++localSuccessfulRemovals;
      }

      continue;
    }

    if (m -> insert(randomKey, i)) {
      ++localSuccessfulInsertions;
    }
  }

  printf("thread %d did %d insertions, %d removals\n", number, localSuccessfulInsertions, localSuccessfulRemovals);

  std::atomic_fetch_add(successfulInsertions, localSuccessfulInsertions);
  std::atomic_fetch_add(successfulRemovals, localSuccessfulRemovals);
}

TEST_F(ThreadSafetyTests, random_actions_when_map_doesnt_grow) {
  std::atomic<int> successfulInsertions, successfulRemovals;
  std::atomic_init(&successfulInsertions, 0);
  std::atomic_init(&successfulRemovals, 0);

  srand(time(nullptr));

  std::thread t1 (randomActionsThread, 1, m, &successfulInsertions, &successfulRemovals);
  std::thread t2 (randomActionsThread, 2, m, &successfulInsertions, &successfulRemovals);
  std::thread t3 (randomActionsThread, 3, m, &successfulInsertions, &successfulRemovals);

  t1.join();
  t2.join();
  t3.join();

  auto elementsInMap = successfulInsertions - successfulRemovals;
  printf("test finished with %d elements in the map\n", elementsInMap);
  EXPECT_GE(size, elementsInMap);
  EXPECT_LE(size*0.9, elementsInMap);
}
