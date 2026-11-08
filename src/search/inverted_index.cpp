#include "search/inverted_index.hpp"

#include <algorithm>

namespace filesearch {

void InvertedIndex::add_posting(const std::string& term, const Posting& p) {
  postings_[term].push_back(p);
  doc_terms_[p.doc_id].push_back(term);
}

void InvertedIndex::add_document(int64_t doc_id, const std::vector<TermCount>& terms) {
  remove_document(doc_id);
  doc_terms_[doc_id];  // a document with no terms still counts
  for (const auto& t : terms) add_posting(t.term, Posting{doc_id, t.tf_name, t.tf_path});
}

void InvertedIndex::remove_document(int64_t doc_id) {
  auto it = doc_terms_.find(doc_id);
  if (it == doc_terms_.end()) return;
  for (const auto& term : it->second) {
    auto pit = postings_.find(term);
    if (pit == postings_.end()) continue;
    auto& list = pit->second;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const Posting& p) { return p.doc_id == doc_id; }),
               list.end());
    if (list.empty()) postings_.erase(pit);
  }
  doc_terms_.erase(it);
}

const std::vector<Posting>* InvertedIndex::postings(const std::string& term) const {
  auto it = postings_.find(term);
  return it == postings_.end() ? nullptr : &it->second;
}

size_t InvertedIndex::document_frequency(const std::string& term) const {
  auto it = postings_.find(term);
  return it == postings_.end() ? 0 : it->second.size();
}

std::vector<std::pair<std::string, size_t>> InvertedIndex::vocabulary_matching(
    const std::string& token) const {
  std::vector<std::pair<std::string, size_t>> out;
  for (const auto& [term, list] : postings_)
    if (term.find(token) != std::string::npos) out.emplace_back(term, list.size());
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace filesearch
