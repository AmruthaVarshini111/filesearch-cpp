#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "search/inverted_index.hpp"

namespace filesearch {

/// How a vocabulary term relates to a query word.
enum class MatchKind { Exact, Prefix, Substring };

double match_weight(MatchKind kind);
/// Precondition: `term` contains `token`.
MatchKind classify_match(const std::string& token, const std::string& term);

struct MatchedTerm {
  std::string term;
  MatchKind kind = MatchKind::Exact;
};

/// One word of the query together with the vocabulary terms it expands to.
struct QueryToken {
  std::string text;
  std::vector<MatchedTerm> matches;
};

struct RankOptions {
  bool name_only = false;  // ignore matches in the directory path
  bool match_any = false;  // OR semantics instead of AND
};

struct ScoredDoc {
  int64_t doc_id = 0;
  double name_score = 0;  // TF-IDF contribution from the file name
  double path_score = 0;  // TF-IDF contribution from the directory path
  uint32_t tokens_matched = 0;
  uint32_t tokens_in_name = 0;
};

/// Smoothed inverse document frequency: ln(1 + N / df).
double idf(uint64_t total_docs, uint64_t df);

constexpr double kNameWeight = 3.0;  // a hit in the file name counts 3x a hit in the path
constexpr double kPathWeight = 1.0;

/// TF-IDF scoring over an in-memory inverted index.
///
/// For every query word q and document d:
///   s(q, d) = max over matched terms t of
///             match_weight(t) * idf(t) * (kNameWeight * (1 + ln tf_name)
///                                         + kPathWeight * (1 + ln tf_path))
/// (a field contributes only when its tf is > 0). Document scores are the sum
/// over query words; with AND semantics a document must match every word.
std::vector<ScoredDoc> rank(const InvertedIndex& index, uint64_t total_docs,
                            const std::vector<QueryToken>& query, const RankOptions& options);

/// Turns raw scores into the final relevance value once the file name is
/// known: the name part is length-normalised (1/sqrt(words in name)), and
/// documents get multiplicative boosts when every query word appears in the
/// name (x1.5), when the stem equals the query (x2.5), or when the whole file
/// name equals the query (x4).
double finalize_score(const ScoredDoc& doc, int name_word_count, size_t query_word_count,
                      const std::string& file_name, const std::string& query_lower);

}  // namespace filesearch
