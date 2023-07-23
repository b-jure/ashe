#include <gtest/gtest.h>
#include <string.h>

#undef _DEFAULT_SOURCE

extern "C" {
#include "../src/ashe_string.h"
}

TEST(StringTest, CreateAndDrop) {
  string_t *str = string_new();
  EXPECT_NE(str, nullptr);

  string_drop(str);

  str = string_from("This is input line\n");
  EXPECT_NE(str, nullptr);
  EXPECT_STREQ(string_ref(str), "This is input line\n");

  string_drop(str);
}

TEST(StringTest, Indexing) {
  string_t *str = string_from("This is input line\n");
  EXPECT_FALSE(str == nullptr);

  size_t len = string_len(str);
  EXPECT_EQ(len, strlen("This is input line\n"));

  byte *slice = string_slice(str, 0);
  EXPECT_STREQ(slice, "This is input line\n");
  slice = string_slice(str, 3);
  EXPECT_STREQ(slice, "s is input line\n");
  slice = string_slice(str, 8);
  EXPECT_STREQ(slice, "input line\n");

  char last = string_last(str);
  EXPECT_EQ(last, '\n');
  char first = string_first(str);
  EXPECT_EQ(first, 'T');

  string_t *new_str = string_from("This is input line\n");
  EXPECT_FALSE(new_str == nullptr);
  EXPECT_TRUE(string_eq(str, new_str));

  string_drop(new_str);
  string_drop(str);
}

TEST(StringTest, Manipulation) {
  string_t *str = string_from("This is input line\n");
  EXPECT_FALSE(str == nullptr);
  size_t oldlen = string_len(str);
  const byte *append_str = "Appended line\n";
  size_t appendlen = strlen(append_str);
  string_append(str, append_str, appendlen);
  EXPECT_EQ(string_len(str), oldlen + appendlen);
  EXPECT_EQ(string_len(str), strlen("This is input line\nAppended line\n"));
  EXPECT_STREQ(string_ref(str), "This is input line\nAppended line\n");

  EXPECT_TRUE(string_remove(str, 5));
  EXPECT_STREQ(string_ref(str), "This s input line\nAppended line\n");
  EXPECT_TRUE(string_remove(str, 5));
  EXPECT_STREQ(string_ref(str), "This  input line\nAppended line\n");
  EXPECT_TRUE(string_remove(str, 5));
  EXPECT_STREQ(string_ref(str), "This input line\nAppended line\n");

  byte *slice = string_slice(str, 5);
  EXPECT_STREQ(slice, "input line\nAppended line\n");
  string_remove_at_ptr(str, slice);
  EXPECT_STREQ(string_ref(str), "This nput line\nAppended line\n");

  ASSERT_TRUE(string_set(str, ' ', 5));
  EXPECT_STREQ(string_ref(str), "This  put line\nAppended line\n");
  ASSERT_EQ(string_get(str, 5), ' ');

  string_drop(str);
}
