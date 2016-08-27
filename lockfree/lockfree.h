#ifndef __LOCKFREE_H
#define __LOCKFREE_H

#include <memory>
#include <memory.h>
#include <atomic>
#include <limits>

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

template <typename Tkey, typename Tvalue, typename Tkey_traits = key_traits<Tvalue>, typename Tvalue_traits = value_traits<Tvalue>>
class LockFreeMap {
public:
  using KeyType = Tkey;
  using ValueType = Tvalue;
  using KeyTraitsType = Tkey_traits;
  using ValueTraitsType = Tvalue_traits;

  LockFreeMap(): LockFreeMap(1000) {}
  ~LockFreeMap() {}

  LockFreeMap(int initialSize, double maxLoadFactor = 0.5, double growthFactor = 4.0): m_maxLoadFactor(maxLoadFactor), m_growthFactor(growthFactor), m_isMigrating(false) {
    m_activeTable = new Table(initialSize, initialSize * maxLoadFactor);
  }

  ValueType insert(KeyType k, ValueType v) {
    Table* table = m_activeTable.load();

    {
      ChangingThread threadRegister(table);

      auto cell = table->fillFirstCellFor(k);
      if (cell == nullptr) {
        return ValueTraitsType::defaultValue();
      }

      cell->value.store(v, std::memory_order::memory_order_release);
    }

    ++table->m_heldKeys;
    if (--table->m_freeCells == 0) {
      migrate();
    }

    return v;
  }

  ValueType get(KeyType k) {
    Table* activeTable = m_activeTable.load();
    auto cell = activeTable->findFirstCellFor(k);

    auto mostRecentValue = cell == nullptr ? ValueTraitsType::defaultValue() : cell->value.load(std::memory_order::memory_order_relaxed);

    if (cell != nullptr || !m_isMigrating.load()) {
      return mostRecentValue;
    }

    Table* backgroundTable = m_backgroundTable.load();
    auto oldCell = backgroundTable->findFirstCellFor(k);

    return (oldCell == nullptr) ? ValueTraitsType::defaultValue() : cell->value.load(std::memory_order::memory_order_relaxed);
  }

  ValueType remove(KeyType k) {
    Table* table = m_activeTable;
    ChangingThread threadRegister(table);

    auto cell = table->findFirstCellFor(k);

    if (cell == nullptr) {
      return ValueTraitsType::defaultValue();
    }

    auto value = cell->value.exchange(ValueTraitsType::defaultValue(), std::memory_order::memory_order_relaxed);

    if (value != ValueTraitsType::defaultValue()) {
      --table->m_heldKeys;
    }

    return value;
  }

private:
  struct Element {
    std::atomic<KeyType> key;
    std::atomic<ValueType> value;
  };

  struct Table {
    Table(int size, int freeCells): m_size(size), m_freeCells(freeCells), m_changingThreads(0), m_heldKeys(0){
      m_data = new Element[size];
      for (int i = 0; i < size; ++i) {
        m_data[i].value = ValueTraitsType::defaultValue();
        m_data[i].key = KeyTraitsType::defaultValue();
      }
    }

    ~Table() {
      delete[] m_data;
    }

    Element* fillFirstCellFor(KeyType k) {
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

    Element* findFirstCellFor(KeyType k) {
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

    int importFrom(Table* oldTable) {
      ChangingThread(this);

      auto transferredEntries = 0;
      for (auto i = 0; i < oldTable->m_size; ++i) {
        auto key = oldTable->m_data[i].key.load(std::memory_order::memory_order_relaxed);
        auto value = oldTable->m_data[i].value.load(std::memory_order::memory_order_relaxed);

        // if the key is default, the cell is uninitialized
        // if the value is default, the key was deleted
        if (key == KeyTraitsType::defaultValue() || value == ValueTraitsType::defaultValue()) {
          continue;
        }

        auto newCell = fillFirstCellFor(key);

        // override the new cell only if it's empty. if it's not, we might undo an update.
        auto expected = ValueTraitsType::defaultValue();
        transferredEntries += (newCell->value.compare_exchange_strong(expected, value)) ? 1 : 0;
      }

      return transferredEntries;
    }

    void waitForThreadsToLeave() {
      int expected = 0;
      while (!m_changingThreads.compare_exchange_weak(expected, 0)) {}
    }

    int m_size;
    std::atomic<int> m_freeCells;
    std::atomic<int> m_heldKeys;
    std::atomic<int> m_changingThreads;
    Element* m_data;
  };

  class ChangingThread {
  public:
    ChangingThread(Table* table) : m_table(table) {
      ++m_table->m_changingThreads;
    }

    ~ChangingThread() {
      --m_table->m_changingThreads;
    }

  private:
    Table* m_table;
  };

  double m_maxLoadFactor;
  double m_growthFactor;

  std::atomic<Table*> m_activeTable;
  std::atomic<Table*> m_backgroundTable;

  // migration
  std::atomic<bool> m_isMigrating;

  void migrate() {
    Table* currentTable = m_activeTable;

    auto baseNumCells = std::max(currentTable->m_size, currentTable->m_heldKeys + currentTable->m_changingThreads);
    auto newSize = static_cast<int>(baseNumCells * m_growthFactor * m_maxLoadFactor);

    printf("+mig from %d to %d\n", currentTable->m_size, newSize);

    auto newTable = new Table(newSize, newSize);

    // make it the active one
    m_backgroundTable = currentTable;
    m_activeTable = newTable;

    currentTable->waitForThreadsToLeave();

    m_isMigrating = true;

    newTable->importFrom(currentTable);

    m_isMigrating = false;

    // delete m_backgroundTable;

    printf("-mig\n");
  }

};

#endif
