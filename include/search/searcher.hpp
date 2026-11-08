#pragma once
#include <string>
#include <vector>

#include "database/index_store.hpp"
#include "database/types.hpp"

namespace filesearch {

struct SearchOptions {
  std::string query;
  FileFilter filter;
  SortKey sort = SortKey::Relevance;
  bool reverse = false;     // reverse the chosen sort order
  size_t limit = 20;        // 0 = unlimited
  bool name_only = false;   // match only the file name, not its directory
  bool match_any = false;   // OR instead of AND between words
  bool exact = false;       // the file name must equal the query (case-insensitive)
  bool partial = true;      // allow prefix / substring matches of words
};

struct SearchResult {
  FileRow file;
  double score = 0;
};

struct SearchOutcome {
  std::vector<SearchResult> results;  // at most `limit`
  size_t total_matches = 0;           // before applying the limit
  double elapsed_ms = 0;
};

/// Runs queries against an IndexStore.
///
/// Pipeline: tokenise the query -> expand every word to matching vocabulary
/// terms -> load their postings into an in-memory InvertedIndex -> TF-IDF
/// ranking -> apply filters while fetching file rows -> sort and limit.
class Searcher {
 public:
  explicit Searcher(IndexStore& store) : store_(store) {}
  SearchOutcome search(const SearchOptions& options);

 private:
  IndexStore& store_;
};

}  // namespace filesearch
