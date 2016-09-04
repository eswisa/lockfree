#include "gtest/gtest.h"
#include "lockfree/lockfree.h"
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>
#include <vector>
#include <stdlib.h>

class ThreadSafetyTests : public ::testing::Test {
public:
  ThreadSafetyTests() {
    m = new LockFreeMap<int, int>(size);
  }

  ~ThreadSafetyTests() {
    delete m;
  }

protected:
  LockFreeMap<int, int>* m;
  int size = 32000;
};

class SafeQueue {

  std::vector<std::pair<std::string, bool>> q;
  std::mutex m;

public:
  void insert(std::string message, bool valid) {
    std::lock_guard<std::mutex> lg(m);
    q.push_back(std::make_pair(message, valid));
  }

  std::vector<std::pair<std::string, bool>>& queue() { return q; }
};

struct TestData {
  std::function<void(int, LockFreeMap<int, int>*, SafeQueue*)> threadFunc;
  std::string funcDescription;
  int numOfThreads;
};

::std::ostream& operator<<(::std::ostream& os, const TestData& testData) {
  return os << "(" << testData.funcDescription << ", " << testData.numOfThreads << " threads)";
}

class ThreadSafetyTestsP : public ::testing::TestWithParam<TestData> {};

void dummyThread(int id, LockFreeMap<int, int>* m, SafeQueue* messages) {
  m->insert(id, id*10);

  std::stringstream strm;
  strm << "thread " << id << " finished it's work";
  //messages->insert(strm.str(), id!=7);
}

void insertDistinct(int id, LockFreeMap<int, int>* m, SafeQueue* messages) {
  auto successes = 0;
  auto base = id * id * 100000;
  for (int i = 1; i <= 33000; ++i) {
    while (m -> insert(base + i, i) == 0) {}
    ++successes;
  }

  if (successes != 33000) {
    std::stringstream strm;
    strm << "thread " << id << " failed to complete all insertions (" << successes << "/33000)";
    messages->insert(strm.str(), false);
  }
}

template <int N>
void insertGetActions(int id, LockFreeMap<int, int>* m, SafeQueue* messages) {
  bool knownKeys[N];
  for (auto i = 0; i < N; ++i) knownKeys[i] = false;

  for (int i = 1; i <= 40000; ++i) {
    auto randomCell = rand() % N;
    auto key = randomCell + 1;
    auto action = rand() % 2;
    int value;

    switch (action) {
      case 0: //insert
        while (m -> insert(key, randomCell + (id * 1000000)) == 0) {}
        knownKeys[randomCell] = true;
        break;
      case 1: // get
        if (knownKeys[randomCell]) {
          value = m->get(key);
          if (value % 1000000 != randomCell) {
            std::stringstream strm;
            strm << "thread " << id << " found entry(" << key << ", " << value << ")";
            messages->insert(strm.str(), false);
            return;
          }
        }
        break;
    }
  }
}

INSTANTIATE_TEST_CASE_P(ThreadSafetyTestsInstantiation, ThreadSafetyTestsP,
  ::testing::Values(TestData{dummyThread, "dummy", 100},
                    TestData{insertDistinct, "inserting (insert-storm)", 10},
                    TestData{insertGetActions<5000>, "insert and get", 10},
                    TestData{insertGetActions<50000>, "insert and get", 10}));

TEST_P(ThreadSafetyTestsP, simpleTest) {
  srand(time(nullptr));

  auto p = GetParam();

  std::thread** threads = new std::thread*[p.numOfThreads];
  LockFreeMap<int, int> m(100000);
  SafeQueue messages;

  for (auto i = 0; i < p.numOfThreads; ++i)
  {
    threads[i] = new std::thread(p.threadFunc, i+1, &m, &messages);
  }

  for (auto i = 0; i < p.numOfThreads; ++i)
  {
    threads[i]->join();
    delete threads[i];
  }

  for (auto &p : messages.queue()) {
    ASSERT_TRUE(p.second) << p.first.c_str();
  }

  delete[] threads;
}

template <int N = 32000>
void randomActionsThread(int number, LockFreeMap<int, int>* m, std::atomic<int>* successfulInsertions, std::atomic<int>* successfulRemovals) {
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

TEST_F(ThreadSafetyTests, DISABLED_random_actions_when_map_doesnt_grow) {
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
