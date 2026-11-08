#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "database/types.hpp"

namespace filesearch {

struct Posting {
  int64_t doc_id = 0;
  uint32_t tf_name = 0;
  uint32_t tf_path = 0;
};

/// In-memory inverted index: term -> postings list, backed by a hash map.
///
/// The persistent copy lives in SQLite (see IndexStore). A query loads only
/// the postings of the terms it needs into one of these, and the ranker works
/// on it. The class is also usable on its own, which is how the unit tests
/// exercise construction and ranking without a database.
class InvertedIndex {
 public:
  void add_document(int64_t doc_id, const std::vector<TermCount>& terms);
  void add_posting(const std::string& term, const Posting& posting);
  void remove_document(int64_t doc_id);

  /// nullptr when the term is unknown.
  const std::vector<Posting>* postings(const std::string& term) const;
  size_t document_frequency(const std::string& term) const;
  size_t term_count() const { return postings_.size(); }
  size_t document_count() const { return doc_terms_.size(); }

  /// Vocabulary terms that contain `token` (including equal terms), with df.
  std::vector<std::pair<std::string, size_t>> vocabulary_matching(const std::string& token) const;

 private:
  std::unordered_map<std::string, std::vector<Posting>> postings_;
  std::unordered_map<int64_t, std::vector<std::string>> doc_terms_;
};

}  // namespace filesearch
