#include "search/ranker.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "utils/string_utils.hpp"

namespace filesearch {

double match_weight(MatchKind kind) {
  switch (kind) {
    case MatchKind::Exact: return 1.0;
    case MatchKind::Prefix: return 0.6;
    case MatchKind::Substring: return 0.3;
  }
  return 0.0;
}

MatchKind classify_match(const std::string& token, const std::string& term) {
  if (term == token) return MatchKind::Exact;
  if (term.compare(0, token.size(), token) == 0) return MatchKind::Prefix;
  return MatchKind::Substring;
}

double idf(uint64_t total_docs, uint64_t df) {
  if (df == 0) return 0.0;
  return std::log(1.0 + static_cast<double>(total_docs) / static_cast<double>(df));
}

namespace {

struct Partial {
  double name = 0;
  double path = 0;
};

// Best score per document for a single query word.
std::unordered_map<int64_t, Partial> score_token(const InvertedIndex& index, uint64_t total_docs,
                                                 const QueryToken& token,
                                                 const RankOptions& opt) {
  std::unordered_map<int64_t, Partial> best;
  for (const auto& m : token.matches) {
    const auto* list = index.postings(m.term);
    if (!list) continue;
    const double weight = match_weight(m.kind) * idf(total_docs, list->size());
    for (const auto& p : *list) {
      if (opt.name_only && p.tf_name == 0) continue;
      const double name = p.tf_name ? weight * kNameWeight * (1.0 + std::log(p.tf_name)) : 0.0;
      const double path =
          (!opt.name_only && p.tf_path) ? weight * kPathWeight * (1.0 + std::log(p.tf_path)) : 0.0;
      if (name == 0.0 && path == 0.0) continue;
      auto& slot = best[p.doc_id];
      slot.name = std::max(slot.name, name);
      slot.path = std::max(slot.path, path);
    }
  }
  return best;
}

}  // namespace

std::vector<ScoredDoc> rank(const InvertedIndex& index, uint64_t total_docs,
                            const std::vector<QueryToken>& query, const RankOptions& options) {
  std::vector<std::unordered_map<int64_t, Partial>> per_token;
  per_token.reserve(query.size());
  for (const auto& token : query) per_token.push_back(score_token(index, total_docs, token, options));
  if (per_token.empty()) return {};

  std::unordered_map<int64_t, ScoredDoc> combined;
  if (options.match_any) {
    for (const auto& map : per_token)
      for (const auto& [doc, part] : map) {
        auto& d = combined[doc];
        d.doc_id = doc;
        d.name_score += part.name;
        d.path_score += part.path;
        ++d.tokens_matched;
        if (part.name > 0) ++d.tokens_in_name;
      }
  } else {
    // AND: iterate the smallest candidate set and probe the others.
    size_t smallest = 0;
    for (size_t i = 1; i < per_token.size(); ++i)
      if (per_token[i].size() < per_token[smallest].size()) smallest = i;
    for (const auto& entry : per_token[smallest]) {
      const int64_t doc = entry.first;
      ScoredDoc d;
      d.doc_id = doc;
      bool all = true;
      for (const auto& map : per_token) {
        auto it = map.find(doc);
        if (it == map.end()) {
          all = false;
          break;
        }
        d.name_score += it->second.name;
        d.path_score += it->second.path;
        ++d.tokens_matched;
        if (it->second.name > 0) ++d.tokens_in_name;
      }
      if (all) combined.emplace(doc, d);
    }
  }

  std::vector<ScoredDoc> out;
  out.reserve(combined.size());
  for (auto& [_, d] : combined) out.push_back(d);
  return out;
}

double finalize_score(const ScoredDoc& doc, int name_word_count, size_t query_word_count,
                      const std::string& file_name, const std::string& query_lower) {
  double score = doc.name_score / std::sqrt(static_cast<double>(std::max(1, name_word_count))) +
                 doc.path_score;
  if (query_word_count > 0 && doc.tokens_in_name == query_word_count) score *= 1.5;
  const std::string name = strutil::to_lower(file_name);
  if (!query_lower.empty()) {
    if (name == query_lower) {
      score *= 4.0;
    } else {
      const auto dot = name.rfind('.');
      if (dot != std::string::npos && dot > 0 && name.compare(0, dot, query_lower) == 0)
        score *= 2.5;
    }
  }
  return score;
}

}  // namespace filesearch
