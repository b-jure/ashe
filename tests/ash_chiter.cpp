#include <gtest/gtest.h>
#include <string.h>

#undef _DEFAULT_SOURCE

extern "C" {
#include "../src/chiter.h"
}

TEST(CharIterTest, Indexing) {
  byte sample[] = "Sample text, iterate me\n";
  chariter_t iter = chariter_new(sample, strlen(sample));

  EXPECT_EQ(chariter_peek(&iter), 'S');
  EXPECT_EQ(chariter_next(&iter), 'S');
  EXPECT_EQ(chariter_next(&iter), 'a');
  EXPECT_EQ(chariter_next(&iter), 'm');
  EXPECT_EQ(chariter_next(&iter), 'p');
  EXPECT_EQ(chariter_next(&iter), 'l');
  EXPECT_EQ(chariter_next(&iter), 'e');
  EXPECT_EQ(chariter_next(&iter), ' ');
  EXPECT_EQ(chariter_next(&iter), 't');
  EXPECT_EQ(chariter_next(&iter), 'e');
  EXPECT_EQ(chariter_next(&iter), 'x');
  EXPECT_EQ(chariter_next(&iter), 't');
  EXPECT_EQ(chariter_next(&iter), ',');
  EXPECT_EQ(chariter_next(&iter), ' ');
  EXPECT_EQ(chariter_next(&iter), 'i');
  EXPECT_EQ(chariter_next(&iter), 't');
  EXPECT_EQ(chariter_next(&iter), 'e');
  EXPECT_EQ(chariter_next(&iter), 'r');
  EXPECT_EQ(chariter_next(&iter), 'a');
  EXPECT_EQ(chariter_next(&iter), 't');
  EXPECT_EQ(chariter_next(&iter), 'e');
  EXPECT_EQ(chariter_next(&iter), ' ');
  EXPECT_EQ(chariter_next(&iter), 'm');
  EXPECT_EQ(chariter_next(&iter), 'e');
  EXPECT_EQ(chariter_next(&iter), '\n');
  EXPECT_EQ(chariter_next(&iter), EOL);
  EXPECT_EQ(chariter_next(&iter), EOL);
  EXPECT_EQ(chariter_goback_unsafe(&iter, 2), -1);
  EXPECT_EQ(chariter_next(&iter), EOL);
  EXPECT_EQ(chariter_next(&iter), EOL);
}
