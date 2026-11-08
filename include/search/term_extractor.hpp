#pragma once
#include <string>
#include <vector>

#include "database/types.hpp"

namespace filesearch {

struct DocumentTerms {
  std::vector<TermCount> terms;  // sorted by term, one entry per distinct term
  int name_token_count = 1;      // words in the file name (>= 1)
};

/// Turns a file name and its directory into the terms stored in the index.
/// Name and directory are separate fields so ranking can weight them
/// differently.
DocumentTerms extract_terms(const std::string& file_name, const std::string& directory);

}  // namespace filesearch
