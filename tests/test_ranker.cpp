#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "search/inverted_index.hpp"
#include "search/ranker.hpp"
#include "search/term_extractor.hpp"
#include "utils/string_utils.hpp"

using namespace filesearch;

namespace {

struct Corpus {
  InvertedIndex index;
  std::vector<std::string> names;  // document id = position + 1
  std::vector<int> name_words;

  void add(const std::string& name, const std::string& dir) {
    const auto doc = extract_terms(name, dir);
    names.push_back(name);
    name_words.push_back(doc.name_token_count);
    index.add_document(static_cast<int64_t>(names.size()), doc.terms);
  }
};

QueryToken make_token(const Corpus& c, const std::string& word) {
  QueryToken t;
  t.text = word;
  for (auto& [term, df] : c.index.vocabulary_matching(word))
    t.matches.push_back({term, classify_match(word, term)});
  return t;
}

// File names ordered by final score, best first.
std::vector<std::string> ranked(const Corpus& c, const std::string& query, RankOptions opt = {}) {
  const std::string lower = strutil::to_lower(query);
  std::vector<QueryToken> tokens;
  for (const auto& w : strutil::split_words(lower)) tokens.push_back(make_token(c, w));
  const auto docs = rank(c.index, c.names.size(), tokens, opt);
  std::vector<std::pair<double, std::string>> scored;
  for (const auto& d : docs) {
    const size_t i = static_cast<size_t>(d.doc_id) - 1;
    scored.emplace_back(finalize_score(d, c.name_words[i], tokens.size(), c.names[i], lower), c.names[i]);
  }
  std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
  std::vector<std::string> out;
  for (auto& s : scored) out.push_back(s.second);
  return out;
}

Corpus sample() {
  Corpus c;
  c.add("machine-learning-notes.txt", "/docs/study");   // 1
  c.add("learning.pdf", "/docs/books");                  // 2
  c.add("holiday.jpg", "/photos/learning");              // 3: only the directory matches
  c.add("budget.csv", "/docs/finance");                  // 4
  c.add("machine-shop-invoice.pdf", "/docs/finance");    // 5
  return c;
}

}  // namespace

TEST(Ranker, IdfGrowsAsTermsGetRarer) {
  EXPECT_GT(idf(100, 1), idf(100, 10));
  EXPECT_GT(idf(100, 10), idf(100, 100));
  EXPECT_GT(idf(100, 100), 0.0);
  EXPECT_EQ(idf(100, 0), 0.0);
}

TEST(Ranker, MatchKindsAreClassified) {
  EXPECT_EQ(classify_match("learn", "learn"), MatchKind::Exact);
  EXPECT_EQ(classify_match("learn", "learning"), MatchKind::Prefix);
  EXPECT_EQ(classify_match("earn", "learning"), MatchKind::Substring);
  EXPECT_GT(match_weight(MatchKind::Exact), match_weight(MatchKind::Prefix));
  EXPECT_GT(match_weight(MatchKind::Prefix), match_weight(MatchKind::Substring));
}

TEST(Ranker, MultipleWordsUseAndSemantics) {
  const auto c = sample();
  const auto r = ranked(c, "machine learning");
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0], "machine-learning-notes.txt");
}

TEST(Ranker, AnySemanticsReturnsTheUnion) {
  const auto c = sample();
  RankOptions o;
  o.match_any = true;
  const auto r = ranked(c, "machine learning", o);
  EXPECT_EQ(r.size(), 4u);
  EXPECT_EQ(r.front(), "machine-learning-notes.txt");  // matches both words, so it leads
}

TEST(Ranker, FileNameHitsBeatDirectoryHits) {
  const auto c = sample();
  const auto r = ranked(c, "learning");
  ASSERT_EQ(r.size(), 3u);
  EXPECT_EQ(r.back(), "holiday.jpg");
}

TEST(Ranker, ShorterNamesRankHigherForTheSameHit) {
  const auto c = sample();
  const auto r = ranked(c, "learning");
  // "learning.pdf" (2 words) beats "machine-learning-notes.txt" (4 words).
  EXPECT_EQ(r[0], "learning.pdf");
}

TEST(Ranker, RarerTermsContributeMore) {
  Corpus c;
  c.add("common-alpha.txt", "/x");
  c.add("common-beta.txt", "/x");
  c.add("common-gamma.txt", "/x");
  c.add("common-rare.txt", "/x");
  RankOptions o;
  o.match_any = true;
  const auto r = ranked(c, "common rare", o);
  EXPECT_EQ(r.front(), "common-rare.txt");
}

TEST(Ranker, TermFrequencyIsSublinear) {
  Corpus c;
  c.add("data-data-data-data-data.txt", "/x");
  c.add("data-aaa-bbb-ccc-ddd.txt", "/x");
  const auto docs = rank(c.index, 2, {make_token(c, "data")}, {});
  ASSERT_EQ(docs.size(), 2u);
  const double many = docs[0].doc_id == 1 ? docs[0].name_score : docs[1].name_score;
  const double once = docs[0].doc_id == 1 ? docs[1].name_score : docs[0].name_score;
  EXPECT_GT(many, once);
  EXPECT_LT(many, 5 * once);  // five occurrences are worth far less than 5x
  EXPECT_NEAR(many / once, 1.0 + std::log(5.0), 1e-9);
}

TEST(Ranker, ExactFilenameMatchGetsTheBiggestBoost) {
  Corpus c;
  c.add("report.pdf", "/a");
  c.add("report-final-draft.pdf", "/a");
  c.add("report.txt", "/a");
  const auto r = ranked(c, "report.pdf");
  ASSERT_FALSE(r.empty());
  EXPECT_EQ(r[0], "report.pdf");
  const auto stem = ranked(c, "report");
  ASSERT_GE(stem.size(), 3u);
  EXPECT_NE(stem.back(), "report.pdf");  // stem match outranks the longer name
}

TEST(Ranker, NameOnlyIgnoresDirectoryMatches) {
  const auto c = sample();
  RankOptions o;
  o.name_only = true;
  const auto r = ranked(c, "learning", o);
  EXPECT_EQ(r.size(), 2u);
  for (const auto& n : r) EXPECT_NE(n, "holiday.jpg");
}

TEST(Ranker, PartialMatchesRankBelowWholeWords) {
  Corpus c;
  c.add("learning.txt", "/x");
  c.add("relearning.txt", "/x");  // contains "learning" only inside a longer word
  const auto r = ranked(c, "learning");
  ASSERT_EQ(r.size(), 2u);
  EXPECT_EQ(r[0], "learning.txt");
}

TEST(Ranker, UnknownWordYieldsNothingUnderAnd) {
  const auto c = sample();
  EXPECT_TRUE(ranked(c, "machine zebra").empty());
}
