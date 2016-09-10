#include "gtest/gtest.h"
#include "lockfree/table.h"

TEST(TableTests, Error_when_there_are_more_free_cells_than_cells) {
  EXPECT_ANY_THROW( (Table<int, int>(1,2)) );
}

TEST(TableTests, Error_when_the_number_of_cells_is_zero) {
  EXPECT_ANY_THROW((Table<int, int>(0,2)));
}

TEST(TableTests, Error_when_the_number_of_cells_is_negative) {
  EXPECT_ANY_THROW((Table<int, int>(-1,2)));
}

TEST(TableTests, Finding_on_an_empty_table) {
  Table<int, int> t(10, 10);

  EXPECT_EQ(nullptr, t.findFirstCellFor(9));
}

TEST(TableTests, Ask_for_a_cell_and_receive) {
  Table<int, int> t(10, 10);
  auto cell = t.fillFirstCellFor(9);

  auto foundCell = t.findFirstCellFor(9);
  EXPECT_NE(nullptr, foundCell);
  EXPECT_EQ(9, foundCell->key.load());
  EXPECT_EQ(0, foundCell->value.load());
}

TEST(TableTests, Ask_for_a_cell_twice) {
  Table<int, int> t(10, 10);
  auto cell1 = t.fillFirstCellFor(9);
  auto cell2 = t.fillFirstCellFor(9);

  EXPECT_EQ(cell1, cell2);
}

TEST(TableTests, Ask_for_a_cell_twice_second_time_no_space_left) {
  Table<int, int> t(10, 3);
  auto cell1 = t.fillFirstCellFor(1);
  auto cell2 = t.fillFirstCellFor(2);
  auto cell3 = t.fillFirstCellFor(3);

  EXPECT_EQ(cell1, t.findFirstCellFor(1));
}

TEST(TableTests, Find_a_cell_when_map_contains_more_elements) {
  Table<int, int> t(10, 3);
  auto cell1 = t.fillFirstCellFor(1);
  auto cell2 = t.fillFirstCellFor(2);
  auto cell3 = t.fillFirstCellFor(3);

  ASSERT_NE(nullptr, cell1);
  ASSERT_NE(nullptr, cell2);
  ASSERT_NE(nullptr, cell3);
  EXPECT_EQ(cell2, t.findFirstCellFor(2));
}

TEST(TableTests, When_full_cant_fill_anymore) {
  Table<int, int> t(3, 3);
  auto cell1 = t.fillFirstCellFor(1);
  auto cell2 = t.fillFirstCellFor(2);
  auto cell3 = t.fillFirstCellFor(3);

  ASSERT_NE(cell1, cell2);
  ASSERT_NE(cell2, cell3);
  ASSERT_NE(cell3, cell1);

  EXPECT_EQ(nullptr, t.fillFirstCellFor(4));
}

TEST(TableTests, When_full_CAN_fill_even_if_there_are_still_empty_cells) {
  Table<int, int> t(4, 3);
  auto cell1 = t.fillFirstCellFor(1);
  auto cell2 = t.fillFirstCellFor(2);
  auto cell3 = t.fillFirstCellFor(3);

  ASSERT_NE(cell1, cell2);
  ASSERT_NE(cell2, cell3);
  ASSERT_NE(cell3, cell1);

  EXPECT_NE(nullptr, t.fillFirstCellFor(4));
}

struct custom_key_traits {
  static int defaultValue() { return 0; }
  static uint32_t hash (int n) {
    return n % 10;
  }
};
TEST(TableTests, When_two_keys_have_the_same_hash) {
  Table<int, int, custom_key_traits> t(10, 10);
  auto cell1 = t.fillFirstCellFor(9);
  auto cell2 = t.fillFirstCellFor(19);

  EXPECT_NE(cell1, cell2);
}

TEST(TableTests, Sanity_for_when_types_are_char) {
  Table<char, char> t(10, 10);

  auto emptyCell = t.findFirstCellFor('a');
  ASSERT_EQ(nullptr, emptyCell);

  auto usedCell = t.fillFirstCellFor('a');
  ASSERT_EQ('a', usedCell->key.load());
  ASSERT_EQ(0, usedCell->value.load());

  auto foundCell = t.findFirstCellFor('a');
  EXPECT_EQ(usedCell, foundCell);
  ASSERT_EQ('a', foundCell->key.load());
  ASSERT_EQ(0, foundCell->value.load());
}

TEST(TableTests, Sanity_for_when_types_are_longlongs) {
  Table<long long, long long> t(10, 10);

  long long k = 2405237205;

  auto emptyCell = t.findFirstCellFor(k);
  ASSERT_EQ(nullptr, emptyCell);

  auto usedCell = t.fillFirstCellFor(k);
  ASSERT_EQ(k, usedCell->key.load());
  ASSERT_EQ(0, usedCell->value.load());

  auto foundCell = t.findFirstCellFor(k);
  EXPECT_EQ(usedCell, foundCell);
  ASSERT_EQ(k, foundCell->key.load());
  ASSERT_EQ(0, foundCell->value.load());
}

struct custom_key_traits_pointer {
  static int* defaultValue() { return nullptr; }
  static uint32_t hash (int* n) {
    return *n % 10;
  }
};

TEST(TableTests, Sanity_for_when_types_are_pointers_hashing_on_address) {
  Table<int*, int*, custom_key_traits_pointer> t(10, 10);

  int keyVal = 25;

  int* k = &keyVal;

  auto emptyCell = t.findFirstCellFor(k);
  ASSERT_EQ(nullptr, emptyCell);

  auto usedCell = t.fillFirstCellFor(k);
  ASSERT_EQ(k, usedCell->key.load());
  ASSERT_EQ(nullptr, usedCell->value.load());

  auto foundCell = t.findFirstCellFor(k);
  EXPECT_EQ(usedCell, foundCell);
  ASSERT_EQ(k, foundCell->key.load());
  ASSERT_EQ(nullptr, foundCell->value.load());
}
