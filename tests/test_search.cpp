#include <gtest/gtest.h>

#include <algorithm>

#include "indexer/indexer.hpp"
#include "search/searcher.hpp"
#include "test_helpers.hpp"
#include "utils/error.hpp"

using namespace filesearch;
using namespace filesearch::testing;

namespace {

constexpr int64_t kJan2020 = 1577836800;
constexpr int64_t kJan2023 = 1672531200;
constexpr int64_t kJan2024 = 1704067200;

class SearchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root = copy_sample_data(tmp.path());
    write_bytes(root / "big" / "archive-data.bin", 2000000);
    set_mtime(root / "documents" / "machine-learning-intro.pdf", kJan2020);
    set_mtime(root / "documents" / "machine-learning-notes.txt", kJan2023);
    set_mtime(root / "documents" / "deep_learning_survey.md", kJan2024);
    store = std::make_unique<IndexStore>(":memory:", OpenMode::CreateIfMissing);
    Indexer(*store).index(root, {});
  }

  std::vector<std::string> names(const SearchOptions& o) {
    std::vector<std::string> out;
    for (auto& r : Searcher(*store).search(o).results) out.push_back(r.file.name);
    return out;
  }
  std::vector<std::string> names(const std::string& q) {
    SearchOptions o;
    o.query = q;
    return names(o);
  }
  static bool has(const std::vector<std::string>& v, const std::string& n) {
    return std::find(v.begin(), v.end(), n) != v.end();
  }

  TempDir tmp;
  fs::path root;
  std::unique_ptr<IndexStore> store;
};

}  // namespace

TEST_F(SearchTest, MultipleKeywordsMustAllMatch) {
  const auto r = names("machine learning");
  EXPECT_EQ(r.size(), 2u);
  EXPECT_TRUE(has(r, "machine-learning-notes.txt"));
  EXPECT_TRUE(has(r, "machine-learning-intro.pdf"));
}

TEST_F(SearchTest, SearchIsCaseInsensitive) {
  EXPECT_EQ(names("MACHINE Learning"), names("machine learning"));
  EXPECT_TRUE(has(names("RESUME"), "resume_2024.txt"));
}

TEST_F(SearchTest, AnyModeReturnsTheUnion) {
  SearchOptions o;
  o.query = "resume vacation";
  EXPECT_TRUE(names(o).empty());
  o.match_any = true;
  EXPECT_EQ(names(o).size(), 2u);
}

TEST_F(SearchTest, PartialAndWholeWordMatching) {
  EXPECT_TRUE(has(names("learn"), "deep_learning_survey.md"));   // prefix
  EXPECT_TRUE(has(names("earning"), "deep_learning_survey.md"));  // substring (>= 3 letters)
  SearchOptions o;
  o.query = "learn";
  o.partial = false;
  EXPECT_TRUE(names(o).empty());
  o.query = "learning";
  EXPECT_FALSE(names(o).empty());
}

TEST_F(SearchTest, ShortWordsOnlyMatchPrefixes) {
  EXPECT_FALSE(has(names("ea"), "deep_learning_survey.md"));  // "ea" is inside "learning" only
  EXPECT_TRUE(has(names("le"), "deep_learning_survey.md"));   // but it is a prefix of "learning"
}

TEST_F(SearchTest, CamelCaseAndWholeRunMatching) {
  EXPECT_TRUE(has(names("search engine"), "SearchEngine.cpp"));
  EXPECT_TRUE(has(names("searchengine"), "SearchEngine.cpp"));
}

TEST_F(SearchTest, ExactFilenameMode) {
  SearchOptions o;
  o.query = "Resume_2024.TXT";
  o.exact = true;
  const auto r = names(o);
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0], "resume_2024.txt");
  o.query = "resume";
  EXPECT_TRUE(names(o).empty());  // exact means the whole name
}

TEST_F(SearchTest, ExactNameRanksFirstInNormalSearch) {
  write_file(root / "x" / "report.pdf");
  write_file(root / "x" / "report-final-version-2.pdf");
  Indexer(*store).index(root, {});
  SearchOptions o;
  o.query = "report.pdf";
  EXPECT_EQ(names(o)[0], "report.pdf");
}

TEST_F(SearchTest, ExtensionFilter) {
  SearchOptions o;
  o.query = "machine learning";
  o.filter.extensions = {"pdf"};
  const auto r = names(o);
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0], "machine-learning-intro.pdf");
  o.filter.extensions = {"pdf", "txt"};
  EXPECT_EQ(names(o).size(), 2u);
}

TEST_F(SearchTest, DirectoryFilter) {
  SearchOptions o;
  o.query = "report";
  EXPECT_EQ(names(o).size(), 3u);
  o.filter.directory = (root / "documents" / "reports").string();
  EXPECT_EQ(names(o).size(), 2u);
  o.filter.directory = (root / "code").string();
  EXPECT_TRUE(names(o).empty());
}

TEST_F(SearchTest, SizeFilters) {
  SearchOptions o;
  o.query = "archive";
  o.filter.min_size = 1000000;
  EXPECT_EQ(names(o).size(), 1u);
  o.filter.min_size = 3000000;
  EXPECT_TRUE(names(o).empty());
  o.filter = {};
  o.query = "data";
  o.filter.max_size = 1000;
  EXPECT_FALSE(has(names(o), "archive-data.bin"));
}

TEST_F(SearchTest, ModifiedDateFilters) {
  SearchOptions o;
  o.query = "learning";
  o.filter.modified_after = kJan2023 - 1;
  auto r = names(o);
  EXPECT_EQ(r.size(), 2u);  // notes (2023) and survey (2024)
  EXPECT_FALSE(has(r, "machine-learning-intro.pdf"));
  o.filter.modified_after = std::nullopt;
  o.filter.modified_before = kJan2020 + 10;
  r = names(o);
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0], "machine-learning-intro.pdf");
}

TEST_F(SearchTest, NameOnlyIgnoresDirectoryNames) {
  SearchOptions o;
  o.query = "documents";
  EXPECT_FALSE(names(o).empty());  // directory name matches by default
  o.name_only = true;
  EXPECT_TRUE(names(o).empty());
}

TEST_F(SearchTest, SortBySizeAndDate) {
  SearchOptions o;
  o.query = "learning";
  o.sort = SortKey::Date;
  auto r = names(o);
  ASSERT_EQ(r.size(), 3u);
  EXPECT_EQ(r[0], "deep_learning_survey.md");  // 2024
  EXPECT_EQ(r[2], "machine-learning-intro.pdf");  // 2020
  o.reverse = true;
  EXPECT_EQ(names(o)[0], "machine-learning-intro.pdf");

  SearchOptions big;
  big.query = "a";  // prefix match, many files
  big.sort = SortKey::Size;
  const auto sized = Searcher(*store).search(big).results;
  ASSERT_GE(sized.size(), 2u);
  EXPECT_EQ(sized[0].file.name, "archive-data.bin");
  for (size_t i = 1; i < sized.size(); ++i) EXPECT_GE(sized[i - 1].file.size, sized[i].file.size);
}

TEST_F(SearchTest, LimitReportsTotalMatches) {
  SearchOptions o;
  o.query = "learning";
  o.limit = 1;
  const auto out = Searcher(*store).search(o);
  EXPECT_EQ(out.results.size(), 1u);
  EXPECT_EQ(out.total_matches, 3u);
  o.limit = 0;
  EXPECT_EQ(Searcher(*store).search(o).results.size(), 3u);
}

TEST_F(SearchTest, RelevanceScoresAreDescending) {
  SearchOptions o;
  o.query = "report";
  const auto out = Searcher(*store).search(o);
  for (size_t i = 1; i < out.results.size(); ++i)
    EXPECT_GE(out.results[i - 1].score, out.results[i].score);
}

TEST_F(SearchTest, FilterOnlyListing) {
  SearchOptions o;
  o.filter.extensions = {"pdf"};
  const auto out = Searcher(*store).search(o);
  ASSERT_EQ(out.results.size(), 1u);
  EXPECT_EQ(out.total_matches, 1u);
}

TEST_F(SearchTest, EmptyQueryWithoutFilterIsAnError) {
  SearchOptions o;
  o.query = "  --  ";
  EXPECT_THROW(Searcher(*store).search(o), UsageError);
}

TEST_F(SearchTest, NoMatchesIsNotAnError) {
  EXPECT_TRUE(names("zzzzqqqq").empty());
}

TEST_F(SearchTest, ResultsReflectIncrementalUpdates) {
  EXPECT_TRUE(names("unicorn").empty());
  write_file(root / "documents" / "unicorn-plans.txt");
  fs::remove(root / "documents" / "resume_2024.txt");
  Indexer(*store).index(root, {});
  EXPECT_EQ(names("unicorn").size(), 1u);
  EXPECT_TRUE(names("resume").empty());
}

TEST_F(SearchTest, FindsFilesByDirectoryName) {
  const auto r = names("reports annual");
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0], "annual-report.txt");
}
