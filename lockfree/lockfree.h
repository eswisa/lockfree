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

  LockFreeMap(int initialSize, double maxLoadFactor = 0.5, double growthFactor = 4.0): m_maxLoadFactor(maxLoadFactor), m_growthFactor(growthFactor) {
    m_activeTable = new Table(initialSize, initialSize * maxLoadFactor);
  }

  ValueType insert(KeyType k, ValueType v) {
    Table* table = m_activeTable.load();

    auto valueInserted = insertWithoutAllocate(table, k, v);
    if (valueInserted == ValueTraitsType::defaultValue()) return valueInserted;

    ++table->m_heldKeys;
    if (--table->m_freeCells == 0) {
      activateNewTable();
    }
    else if (!m_oldTables.empty() && m_oldTables.startMigrationTransaction()) {
      AutoCloseMigration endMigration(&m_oldTables);

      auto oldTable = m_oldTables.peekOldest();
      if (migrateFirstElements(oldTable, table, 0)) {
        m_oldTables.discardOldest();
        delete oldTable;
      }
    }



    return v;
  }

  ValueType get(KeyType k) {
    Table* activeTable = m_activeTable.load();
    auto cell = activeTable->findFirstCellFor(k);

    if (cell != nullptr) {
      return cell->value.load(std::memory_order::memory_order_relaxed);
    }

    auto v = m_oldTables.getValueHistorically(k);
    if (ValueTraitsType::defaultValue() != insertWithoutAllocate(activeTable, k, v))
      m_oldTables.removeValueHistorically(k);

    return v;
  }

  ValueType remove(KeyType k) {
    Table* table = m_activeTable;

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
    Table(int size, int freeCells): m_size(size), m_freeCells(freeCells), m_heldKeys(0){
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

    int m_size;
    std::atomic<int> m_freeCells;
    std::atomic<int> m_heldKeys;
    Element* m_data;
  };

  struct OldTablesContainer {
    OldTablesContainer(int size = 100) : m_size(size), m_totalTables(0), m_head(0), m_tail(0), m_isMigrating(false) {
      m_data = new Table*[m_size];
    }

    bool empty() {
      return m_totalTables == 0;
    }

    bool full() {
      return m_totalTables == m_size;
    }

    bool insert(Table* t){
      while (!full()) {
        auto currTail = m_tail.load(std::memory_order::memory_order_relaxed);
        auto newTail = (currTail + 1) % m_size;
        if (m_tail.compare_exchange_strong(currTail, newTail)) {
          ++m_totalTables;
          m_data[currTail] = t;
          return true;
        }
      }
      return false;

    }

    Table* discardOldest() {
      while (!empty()) {
        auto currHead = m_head.load(std::memory_order::memory_order_relaxed);
        auto newHead = (currHead + 1) % m_size;
        if (m_head.compare_exchange_strong(currHead, newHead)) {
          --m_totalTables;
          return m_data[currHead];
        }
      }
      return nullptr;
    }

    Table* peekOldest() {
      auto currHead = m_head.load(std::memory_order::memory_order_relaxed);
      return m_data[currHead];
    }

    ValueType getValueHistorically(KeyType k) {
      auto v = ValueTraitsType::defaultValue();
      for (auto i = m_head.load(std::memory_order::memory_order_seq_cst); i < m_tail.load(); ++i) {
        auto t = m_data[i];
        auto cell = t->findFirstCellFor(k);
        if (cell != nullptr) {
          v = cell->value.load();
        }
      }

      return v;
    }

    void removeValueHistorically(KeyType k) {
      for (auto i = m_head.load(std::memory_order::memory_order_seq_cst); i < m_tail.load(); ++i) {
        auto t = m_data[i];
        auto cell = t->findFirstCellFor(k);
        if (cell != nullptr) {
          cell->key.store(KeyTraitsType::defaultValue(), std::memory_order::memory_order_relaxed);
          cell->value.store(ValueTraitsType::defaultValue(), std::memory_order::memory_order_relaxed);
        }
      }
    }

    bool startMigrationTransaction() {
      auto v = false;
      return m_isMigrating.compare_exchange_strong(v, true);
    }

    void endTransaction() {
      m_isMigrating.store(false, std::memory_order::memory_order_relaxed);
    }

    Table** m_data;
    int m_size;
    std::atomic<int> m_totalTables;
    std::atomic<int> m_head;
    std::atomic<int> m_tail;
    std::atomic<bool> m_isMigrating;
  };

  struct AutoCloseMigration {
    AutoCloseMigration(OldTablesContainer* oldTables): m_container(oldTables) {}
    ~AutoCloseMigration() {
      m_container->endTransaction();
    }

  private:
    OldTablesContainer* m_container;
  };

  double m_maxLoadFactor;
  double m_growthFactor;

  std::atomic<Table*> m_activeTable;
  OldTablesContainer m_oldTables;

  // migration

  void activateNewTable() {
    Table* currentTable = m_activeTable.load();
    auto newSize = static_cast<int>(currentTable->m_size * m_growthFactor);

    auto newTable = new Table(newSize, newSize * m_maxLoadFactor);

    m_oldTables.insert(currentTable);
    m_activeTable = newTable;
  }

  ValueType insertWithoutAllocate(Table* table, KeyType k, ValueType v) {
    auto cell = table->fillFirstCellFor(k);
    if (cell == nullptr) {
      return ValueTraitsType::defaultValue();
    }

    cell->value.store(v, std::memory_order::memory_order_release);
    return v;
  }

  bool migrateFirstElements(Table* fromTable, Table* toTable, int n) {
    auto migratedElements = 0;
    for (auto i = 0; i < fromTable->m_size; ++i) {
      if (migratedElements >= n) return false;
      if (fromTable->m_data[i].key == KeyTraitsType::defaultValue()) continue;

      auto v = m_oldTables.getValueHistorically(fromTable->m_data[i].key);
      if (v == ValueTraitsType::defaultValue()) continue;

      insertWithoutAllocate(toTable, fromTable->m_data[i].key, v);
      m_oldTables.removeValueHistorically(fromTable->m_data[i].key);
      migratedElements++;
    }

    return true;
  }

};

#endif
