#ifndef _LOCKFREE_H
#define _LOCKFREE_H

#include <memory>

class LockfreeMap {
public:
  using KeyType = int;
  using ValueType = int;

  LockfreeMap() {
    m_data = std::unique_ptr<Element>(new Element[32]);
  }

  LockfreeMap(const LockfreeMap& other){}

  bool insert(KeyType k, ValueType v) {
    auto keyInserted = m_data.get()[k].key == 0;

    m_data.get()[k].key = k;
    m_data.get()[k].value = v;
    return keyInserted;
  }

  ValueType remove(KeyType k) {
    m_data.get()[k].key = 0;
    auto v = m_data.get()[k].value;
    m_data.get()[k].value = 0;

    return v;
  }

  ValueType get(KeyType k) {
    return m_data.get()[k].value;
  }

private:
  struct Element {
    KeyType key;
    ValueType value;
  };

  std::unique_ptr<Element> m_data;
};

#endif
