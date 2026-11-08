#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ctime>

#include "utils/format_utils.hpp"
#include "utils/path_utils.hpp"
#include "utils/string_utils.hpp"

using namespace filesearch;
using ::testing::ElementsAre;

TEST(StringUtils, LowerTrimSplitJoin) {
  EXPECT_EQ(strutil::to_lower("ReSuMe.PDF"), "resume.pdf");
  EXPECT_EQ(strutil::trim("  hi \n"), "hi");
  EXPECT_THAT(strutil::split("a,b,,c", ','), ElementsAre("a", "b", "", "c"));
  EXPECT_EQ(strutil::join({"a", "b"}, "-"), "a-b");
  EXPECT_TRUE(strutil::iequals("PDF", "pdf"));
}

TEST(StringUtils, SplitWordsHandlesSeparatorsAndCase) {
  EXPECT_THAT(strutil::split_words("machine-learning_notes.TXT"),
              ElementsAre("machine", "learning", "notes", "txt"));
  EXPECT_THAT(strutil::split_words("  ...  "), ElementsAre());
}

TEST(StringUtils, SplitWordsSplitsCamelCase) {
  EXPECT_THAT(strutil::split_words("SearchEngine.cpp"), ElementsAre("search", "engine", "cpp"));
  EXPECT_THAT(strutil::split_words("PDFReader"), ElementsAre("pdf", "reader"));
  EXPECT_THAT(strutil::split_words("report2024"), ElementsAre("report2024"));
}

TEST(StringUtils, IndexTermsAddsWholeRunForSplitWords) {
  EXPECT_THAT(strutil::index_terms("FileSearch"), ElementsAre("file", "search", "filesearch"));
  EXPECT_THAT(strutil::index_terms("notes"), ElementsAre("notes"));
}

TEST(StringUtils, Utf8BytesAreKeptInsideWords) {
  const auto words = strutil::split_words("Résumé-2024");
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(words[0], "r\xC3\xA9sum\xC3\xA9");  // é untouched, ASCII letters lower-cased
  EXPECT_EQ(words[1], "2024");
}

TEST(StringUtils, GlobMatch) {
  EXPECT_TRUE(strutil::glob_match("*.log", "Debug.LOG"));
  EXPECT_TRUE(strutil::glob_match("node_modules", "node_modules"));
  EXPECT_TRUE(strutil::glob_match("a?c", "abc"));
  EXPECT_FALSE(strutil::glob_match("a?c", "ac"));
  EXPECT_FALSE(strutil::glob_match("*.log", "log.txt"));
  EXPECT_TRUE(strutil::glob_match("*", ""));
}

TEST(StringUtils, EscapingHelpers) {
  EXPECT_EQ(strutil::escape_like("50%_off\\"), "50\\%\\_off\\\\");
  EXPECT_EQ(strutil::json_escape("a\"b\\c\n\x01"), "a\\\"b\\\\c\\n\\u0001");
}

TEST(FormatUtils, ParseSize) {
  EXPECT_EQ(fmt::parse_size("1024"), 1024u);
  EXPECT_EQ(fmt::parse_size("1MB"), 1000000u);
  EXPECT_EQ(fmt::parse_size("1 mb"), 1000000u);
  EXPECT_EQ(fmt::parse_size("1.5KB"), 1500u);
  EXPECT_EQ(fmt::parse_size("2KiB"), 2048u);
  EXPECT_EQ(fmt::parse_size("1G"), 1000000000u);
  EXPECT_EQ(fmt::parse_size("500k"), 500000u);
}

TEST(FormatUtils, ParseSizeRejectsGarbage) {
  for (const char* bad : {"", "abc", "-5", "1XB", "MB", "0x10", "nan", "1e999"})
    EXPECT_FALSE(fmt::parse_size(bad).has_value()) << bad;
}

TEST(FormatUtils, FormatSizeAndNumber) {
  EXPECT_EQ(fmt::format_size(0), "0 B");
  EXPECT_EQ(fmt::format_size(999), "999 B");
  EXPECT_EQ(fmt::format_size(1500), "1.5 KB");
  EXPECT_EQ(fmt::format_size(2500000), "2.5 MB");
  EXPECT_EQ(fmt::format_number(1234567), "1,234,567");
  EXPECT_EQ(fmt::format_number(12), "12");
}

TEST(FormatUtils, ParseTimeRelativeAndDates) {
  const int64_t now = 1700000000;
  EXPECT_EQ(fmt::parse_time("7d", false, now), now - 7 * 86400);
  EXPECT_EQ(fmt::parse_time("2w", false, now), now - 14 * 86400);
  EXPECT_EQ(fmt::parse_time("12h", false, now), now - 12 * 3600);
  EXPECT_EQ(fmt::parse_time("1y", false, now), now - 365 * 86400);

  const auto start = fmt::parse_time("2024-03-10", false, now);
  const auto end = fmt::parse_time("2024-03-10", true, now);
  ASSERT_TRUE(start && end);
  EXPECT_GE(*end - *start, 86399 - 3600);  // allow for a DST change
  EXPECT_LE(*end - *start, 86399 + 3600);

  EXPECT_TRUE(fmt::parse_time("today", false, now));
  EXPECT_LT(*fmt::parse_time("yesterday", false, now), *fmt::parse_time("today", false, now));
}

TEST(FormatUtils, ParseTimeRejectsInvalid) {
  const int64_t now = 1700000000;
  for (const char* bad : {"", "yesterdayish", "2024-02-31", "2024-13-01", "24-01-01", "d", "7x", "2024"})
    EXPECT_FALSE(fmt::parse_time(bad, false, now).has_value()) << bad;
}

TEST(PathUtils, NormalisationAndPrefixes) {
  using namespace pathutil;
  EXPECT_EQ(absolute_normal("/a/b/../c/").string(), "/a/c");
  EXPECT_EQ(absolute_normal("/").string(), "/");
  EXPECT_EQ(dir_prefix("/a"), "/a/");
  EXPECT_EQ(dir_prefix("/"), "/");
  EXPECT_EQ(prefix_upper_bound("/a/"), "/a0");
  EXPECT_TRUE(is_same_or_inside("/a/b", "/a"));
  EXPECT_FALSE(is_same_or_inside("/ab", "/a"));
  EXPECT_EQ(parent_of("/a/b"), "/a");
  EXPECT_EQ(parent_of("/a"), "/");
  EXPECT_EQ(join("/", "x"), "/x");
  EXPECT_EQ(join("/a", "x"), "/a/x");
  EXPECT_TRUE(is_hidden_name(".git"));
  EXPECT_FALSE(is_hidden_name("a.git"));
}

TEST(PathUtils, TildeExpansionUsesHome) {
  setenv("HOME", "/tmp/some-home", 1);
  EXPECT_EQ(pathutil::absolute_normal("~/Documents").string(), "/tmp/some-home/Documents");
  EXPECT_EQ(pathutil::absolute_normal("~").string(), "/tmp/some-home");
}
