#ifndef __LOCKFREE_H
#define __LOCKFREE_H

#include <memory>
#include <atomic>

#include "table.h"

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
    m_activeTable = new Table<Tkey, Tvalue>(initialSize, initialSize * maxLoadFactor);
  }

  ValueType insert(KeyType k, ValueType v) {
    Table<Tkey, Tvalue>* table = m_activeTable.load();

    auto insertionResult = insertWithoutAllocate(table, k, v);
    switch (insertionResult) {
      case InsertionResult::value_updated:
        return v;
      case InsertionResult::insertion_failed:
        return ValueTraitsType::defaultValue();
      case InsertionResult::key_inserted:
        ++table->m_heldKeys;
        if (--table->m_freeCells == 0) {
          activateNewTable();
        }

        return v;
    }
  }

  ValueType get(KeyType k) {
    Table<Tkey, Tvalue>* activeTable = m_activeTable.load();
    auto cell = activeTable->findFirstCellFor(k);

    if (cell != nullptr) {
      return cell->value.load(std::memory_order::memory_order_relaxed);
    }

    auto v = m_oldTables.getValueHistorically(k);
    if (InsertionResult::insertion_failed != insertWithoutAllocate(activeTable, k, v)) {
      m_oldTables.removeValueHistorically(k);
    }

    return v;
  }

  ValueType remove(KeyType k) {
    Table<Tkey, Tvalue>* table = m_activeTable;

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
  struct OldTablesContainer {
    OldTablesContainer(int size = 100) : m_size(size), m_totalTables(0), m_head(0), m_tail(0), m_isMigrating(false) {
      m_data = new Table<Tkey, Tvalue>*[m_size];
    }

    bool empty() {
      return m_totalTables == 0;
    }

    bool full() {
      return m_totalTables == m_size;
    }

    bool insert(Table<Tkey, Tvalue>* t){
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

    Table<Tkey, Tvalue>* discardOldest() {
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

    Table<Tkey, Tvalue>* peekOldest() {
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
          t->m_heldKeys--;
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

    Table<Tkey, Tvalue>** m_data;
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

  enum class InsertionResult {
    value_updated, key_inserted, insertion_failed
  };

  double m_maxLoadFactor;
  double m_growthFactor;

  std::atomic<Table<Tkey, Tvalue>*> m_activeTable;
  OldTablesContainer m_oldTables;

  // migration

  void activateNewTable() {
    Table<Tkey, Tvalue>* currentTable = m_activeTable.load();
    auto newSize = static_cast<int>(currentTable->m_size * m_growthFactor);

    auto newTable = new Table<Tkey, Tvalue>(newSize, newSize * m_maxLoadFactor);

    m_oldTables.insert(currentTable);
    m_activeTable = newTable;
  }

  InsertionResult insertWithoutAllocate(Table<Tkey, Tvalue>* table, KeyType k, ValueType v) {
    auto cell = table->fillFirstCellFor(k);
    if (cell == nullptr) {
      return InsertionResult::insertion_failed;
    }

    auto prev = cell->value.exchange(v, std::memory_order::memory_order_release);
    return prev == ValueTraitsType::defaultValue() ? InsertionResult::key_inserted : InsertionResult::value_updated;
  }

  bool migrateFirstElements(Table<Tkey, Tvalue>* fromTable, Table<Tkey, Tvalue>* toTable, int n) {
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
