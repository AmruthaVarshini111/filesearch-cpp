#include "search/term_extractor.hpp"

#include <algorithm>
#include <unordered_map>

#include "utils/string_utils.hpp"

namespace filesearch {

DocumentTerms extract_terms(const std::string& file_name, const std::string& directory) {
  std::unordered_map<std::string, TermCount> counts;
  for (auto& t : strutil::index_terms(file_name)) {
    auto& c = counts[t];
    if (c.term.empty()) c.term = t;
    ++c.tf_name;
  }
  for (auto& t : strutil::index_terms(directory)) {
    auto& c = counts[t];
    if (c.term.empty()) c.term = t;
    ++c.tf_path;
  }
  DocumentTerms out;
  out.terms.reserve(counts.size());
  for (auto& [_, c] : counts) out.terms.push_back(std::move(c));
  std::sort(out.terms.begin(), out.terms.end(),
            [](const TermCount& a, const TermCount& b) { return a.term < b.term; });
  out.name_token_count = std::max<int>(1, static_cast<int>(strutil::split_words(file_name).size()));
  return out;
}

}  // namespace filesearch
