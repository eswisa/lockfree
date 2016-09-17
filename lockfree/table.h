#ifndef TABLE_H
#define TABLE_H

#include <atomic>
#include <limits>
#include <stdexcept>

template <typename T>
struct key_traits {
  static T defaultValue() { return T(); }
  static uint32_t hash (T n) {
    static_assert(std::is_integral<T>::value, "Key should be integer or a custom key_traits should be used.");
    n ^= n >> 16;
    n *= 0x85ebca6b;
    n ^= n >> 13;
    n *= 0xc2b2ae35;
    n ^= n >> 16;
    return static_cast<uint32_t>(n);
  }
};

template <typename T>
struct value_traits {
  static T defaultValue() { return T(); }
};

template <typename KeyType, typename ValueType>
struct Element {
  std::atomic<KeyType> key;
  std::atomic<ValueType> value;
};

template <typename KeyType, typename ValueType, typename KeyTraitsType = key_traits<KeyType>, typename ValueTraitsType = value_traits<ValueType>>
class Table {

public:
  Table(int size, int freeCells): m_size(size), m_freeCells(freeCells), m_heldKeys(0){
    if (size == 0) throw std::invalid_argument("size argument cannot be 0");
    if (size < 0) throw std::invalid_argument("size argument cannot be negative");
    if (size < freeCells) throw std::invalid_argument("size must not be less than freeCells");

    m_data = new Element<KeyType, ValueType>[size];
    for (int i = 0; i < size; ++i) {
      m_data[i].value = ValueTraitsType::defaultValue();
      m_data[i].key = KeyTraitsType::defaultValue();
    }
  }

  ~Table() {
    delete[] m_data;
  }

  Element<KeyType, ValueType>* fillFirstCellFor(KeyType k) {
    auto totalCells = m_size;

    for (auto idx = KeyTraitsType::hash(k); totalCells > 0; ++idx, --totalCells) {
      idx %= m_size;
      auto currCellKey = std::atomic_load_explicit(&m_data[idx].key, std::memory_order::memory_order_relaxed);

      if ((currCellKey == k) ||
         (currCellKey == KeyTraitsType::defaultValue() && std::atomic_compare_exchange_strong(&m_data[idx].key, &currCellKey, k))) {
        return &m_data[idx];
      }
    }
    return nullptr;
  }

  Element<KeyType, ValueType>* findFirstCellFor(KeyType k) {
    auto totalCells = m_size;

    for (auto idx = KeyTraitsType::hash(k); totalCells > 0; ++idx, --totalCells) {
      idx %= m_size;
      auto currCellKey = std::atomic_load_explicit(&m_data[idx].key, std::memory_order::memory_order_relaxed);

      if (currCellKey == k) {
        return &m_data[idx];
      }
    }
    return nullptr;
  }

  int m_size;
  std::atomic<int> m_freeCells;
  std::atomic<int> m_heldKeys;
  Element<KeyType, ValueType>* m_data;
};

template <typename KeyType, typename ValueType, typename ValueTraitsType = value_traits<ValueType>>
class DecayingTable {

public:
  DecayingTable(Table<KeyType, ValueType>* table) {
    if (table == nullptr) throw std::invalid_argument("table argument cannot be null");

    m_table = table;
    m_active.store(m_table->m_heldKeys > 0, std::memory_order::memory_order_release);
  }

  bool exists(KeyType k) {
    if (isEmpty()) return false;

    return nullptr == m_table->findFirstCellFor(k);
  }

  ValueType get(KeyType k) {
    if (isEmpty()) return ValueTraitsType::defaultValue();

    auto cell = m_table->findFirstCellFor(k);
    if (cell == nullptr)  return ValueTraitsType::defaultValue();

    return cell->value.load(std::memory_order::memory_order_relaxed);
  }

  ValueType remove(KeyType k) {
    if (isEmpty()) return ValueTraitsType::defaultValue();

    auto cell = m_table->findFirstCellFor(k);
    if (cell == nullptr)  return ValueTraitsType::defaultValue();

    auto oldValue = cell->value.load();
    cell->value.store(ValueTraitsType::defaultValue(), std::memory_order::memory_order_relaxed);
    m_table->m_heldKeys--;
    return oldValue;
  }

  inline bool isEmpty() {
    auto isActive = m_active.load(std::memory_order::memory_order_relaxed);
    if (isActive) {
      auto isActiveNewValue = m_table->m_heldKeys.load(std::memory_order::memory_order_relaxed) > 0;
      m_active.store(isActiveNewValue, std::memory_order::memory_order_release);

      // we can safely delete m_table now

      return !isActiveNewValue;
    }
    return !isActive;
  }


private:
  Table<KeyType, ValueType>* m_table;
  std::atomic<bool> m_active;

};
#endif // TABLE_H
