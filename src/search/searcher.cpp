#include "search/searcher.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#include "search/inverted_index.hpp"
#include "search/ranker.hpp"
#include "utils/error.hpp"
#include "utils/string_utils.hpp"

namespace filesearch {
namespace {

// Words shorter than this only match whole terms and prefixes. Substring
// matching of one or two letters would pull in most of the vocabulary.
constexpr size_t kMinSubstringLength = 3;

void sort_results(std::vector<SearchResult>& results, SortKey key, bool reverse) {
  auto by_path = [](const SearchResult& a, const SearchResult& b) { return a.file.path < b.file.path; };
  switch (key) {
    case SortKey::Relevance:
      std::sort(results.begin(), results.end(), [&](const SearchResult& a, const SearchResult& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.file.path.size() != b.file.path.size()) return a.file.path.size() < b.file.path.size();
        return by_path(a, b);
      });
      break;
    case SortKey::Size:
      std::sort(results.begin(), results.end(), [&](const SearchResult& a, const SearchResult& b) {
        if (a.file.size != b.file.size) return a.file.size > b.file.size;
        return by_path(a, b);
      });
      break;
    case SortKey::Date:
      std::sort(results.begin(), results.end(), [&](const SearchResult& a, const SearchResult& b) {
        if (a.file.mtime != b.file.mtime) return a.file.mtime > b.file.mtime;
        return by_path(a, b);
      });
      break;
    case SortKey::Name:
      std::sort(results.begin(), results.end(), [&](const SearchResult& a, const SearchResult& b) {
        const auto an = strutil::to_lower(a.file.name), bn = strutil::to_lower(b.file.name);
        if (an != bn) return an < bn;
        return by_path(a, b);
      });
      break;
  }
  if (reverse) std::reverse(results.begin(), results.end());
}

}  // namespace

SearchOutcome Searcher::search(const SearchOptions& o) {
  const auto t0 = std::chrono::steady_clock::now();
  SearchOutcome out;
  const std::string query_lower = strutil::to_lower(strutil::trim(o.query));
  std::vector<SearchResult> scored;

  auto finish = [&]() {
    out.elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return out;
  };

  if (o.exact) {
    if (query_lower.empty()) throw UsageError("--exact needs a file name to look for");
    for (auto& row : store_.find_by_name(query_lower, o.filter)) scored.push_back({std::move(row), 1.0});
  } else {
    // Distinct query words, in order.
    std::vector<std::string> words;
    {
      std::unordered_set<std::string> seen;
      for (auto& w : strutil::split_words(query_lower))
        if (seen.insert(w).second) words.push_back(w);
    }

    if (words.empty()) {
      if (o.filter.empty())
        throw UsageError(
            "the query contains no searchable words (letters or digits); "
            "add a word or use a filter such as --type");
      // Filter-only listing.
      out.total_matches = store_.count_files(o.filter);
      for (auto& row : store_.query_files(o.filter, o.sort, o.reverse, o.limit))
        out.results.push_back({std::move(row), 0.0});
      return finish();
    }

    std::vector<QueryToken> tokens;
    for (const auto& w : words) {
      QueryToken qt;
      qt.text = w;
      if (o.partial) {
        for (auto& [term, df] : store_.expand_vocabulary(w, w.size() >= kMinSubstringLength))
          qt.matches.push_back({term, classify_match(w, term)});
      } else if (store_.term_df(w) > 0) {
        qt.matches.push_back({w, MatchKind::Exact});
      }
      if (qt.matches.empty() && !o.match_any) return finish();  // AND with an unmatched word
      tokens.push_back(std::move(qt));
    }

    InvertedIndex index;
    std::unordered_set<std::string> loaded;
    for (const auto& t : tokens)
      for (const auto& m : t.matches)
        if (loaded.insert(m.term).second)
          store_.for_each_posting(m.term, [&](int64_t doc, uint32_t tfn, uint32_t tfp) {
            index.add_posting(m.term, Posting{doc, tfn, tfp});
          });

    RankOptions ro;
    ro.name_only = o.name_only;
    ro.match_any = o.match_any;
    const auto docs = rank(index, store_.file_count(), tokens, ro);
    if (docs.empty()) return finish();

    std::vector<int64_t> ids;
    ids.reserve(docs.size());
    for (const auto& d : docs) ids.push_back(d.doc_id);
    std::unordered_map<int64_t, FileRow> rows;
    for (auto& r : store_.fetch_files(ids, o.filter)) {
      const int64_t id = r.id;
      rows.emplace(id, std::move(r));
    }
    for (const auto& d : docs) {
      auto it = rows.find(d.doc_id);
      if (it == rows.end()) continue;  // removed by the filters
      const double score = finalize_score(d, it->second.ntok, tokens.size(), it->second.name, query_lower);
      scored.push_back({std::move(it->second), score});
    }
  }

  out.total_matches = scored.size();
  sort_results(scored, o.sort, o.reverse);
  if (o.limit != 0 && scored.size() > o.limit) scored.resize(o.limit);
  out.results = std::move(scored);
  return finish();
}

}  // namespace filesearch
