#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#undef _DEFAULT_SOURCE

extern "C" {
#include "../src/lexer.h"
}

TEST(LexerTest, ParseTokens) {
  token_t token;
  byte cmdline[100];
  int fd = open("cmdline.txt", O_RDONLY);
  int n = read(fd, cmdline, 100);
  printf("N -> %d\n", n);
  lexer_t lexer = lexer_new(cmdline, strlen(cmdline));

  printf("FILE: %s\n", cmdline);
  printf("LEN: %ld\n", strlen(cmdline));
  token = lexer_next(&lexer);
  EXPECT_TRUE(token.type == WORD_TOKEN);
  EXPECT_STREQ(string_ref(token.contents), "echo");

  token = lexer_peek(&lexer);
  EXPECT_TRUE(token.type == WORD_TOKEN);
  EXPECT_STREQ(string_ref(token.contents), "echo");
  string_drop(token.contents);

  token = lexer_next(&lexer);
  EXPECT_TRUE(token.type == WORD_TOKEN);
  EXPECT_STREQ(string_ref(token.contents), "Hello, World");
  string_drop(token.contents);
}
