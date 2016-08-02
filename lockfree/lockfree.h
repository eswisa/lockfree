#ifndef _LOCKFREE_H
#define _LOCKFREE_H

#include <memory>
#include <atomic>

class LockfreeMap {
public:
  using KeyType = int;
  using ValueType = int;

  LockfreeMap():LockfreeMap(8){}
  LockfreeMap(int size):m_size(size) {
    m_data = new Element[m_size];
    memset(m_data, 0, sizeof(Element) * m_size);
  }

  ~LockfreeMap() {
    delete[] m_data;
  }

  LockfreeMap(const LockfreeMap& other){}

  bool insert(KeyType k, ValueType v) {
    auto cells = m_size;
    for(auto hash = integerHash(k); m_size > 0; ++hash, --m_size) {
      hash %= m_size;
      auto currKey = std::atomic_load_explicit(&m_data[hash].key, std::memory_order::memory_order_relaxed);

      // ~ double locking
      if (currKey == 0) {
        auto ownKey = std::atomic_compare_exchange_strong(&m_data[hash].key, &currKey, k);
        if (ownKey) {
          m_data[hash].value = v; // if not atomic, problemo.
          return true;
        }
      }
    }

    return false;
  }

  ValueType get(KeyType k) {
    auto cells = m_size;
    for(auto hash = integerHash(k); m_size > 0; ++hash, --m_size) {
      hash %= m_size;
      auto currKey = std::atomic_load_explicit(&m_data[hash].key, std::memory_order::memory_order_relaxed);
      if (k == currKey) {
        return m_data[hash].value;
      }
      if (currKey == 0) {
        return 0;
      }
    }
    return 0;
  }

  ValueType remove(KeyType k) {
    m_data[k].key = 0;
    auto v = m_data[k].value;
    m_data[k].value = 0;

    return v;
  }

private:

  inline static uint32_t integerHash(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
  }

  struct Element {
    std::atomic<KeyType> key;
    ValueType value;
  };

  Element* m_data;
  int m_size;

};

#endif
