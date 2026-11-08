#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace filesearch {

/// Metadata of a file as seen on disk.
struct FileMeta {
  std::string path;  // absolute, normalised
  std::string name;  // last path component
  std::string ext;   // lower-case, without the dot ("" if none)
  uint64_t size = 0;
  int64_t mtime = 0;  // seconds since the Unix epoch
};

/// A row of the `files` table.
struct FileRow {
  int64_t id = 0;
  std::string path;
  std::string name;
  std::string ext;
  uint64_t size = 0;
  int64_t mtime = 0;
  int ntok = 1;  // number of words in the file name (for length normalisation)
};

/// Term frequencies of one term in one document, split by field.
struct TermCount {
  std::string term;
  uint32_t tf_name = 0;  // occurrences in the file name
  uint32_t tf_path = 0;  // occurrences in the directory path
};

/// An indexed top-level directory together with the options it was indexed with.
struct RootInfo {
  std::string path;
  bool include_hidden = false;
  bool follow_symlinks = false;
  std::vector<std::string> excludes;
  int64_t last_indexed = 0;
};

enum class SortKey { Relevance, Size, Date, Name };

/// Restrictions applied to search results. All members are optional.
struct FileFilter {
  std::vector<std::string> extensions;  // lower-case, no dot; OR-ed together
  std::optional<std::string> directory;  // absolute path; matches files below it
  std::optional<uint64_t> min_size;
  std::optional<uint64_t> max_size;
  std::optional<int64_t> modified_after;   // inclusive
  std::optional<int64_t> modified_before;  // inclusive

  bool empty() const {
    return extensions.empty() && !directory && !min_size && !max_size && !modified_after &&
           !modified_before;
  }
};

struct StoreStats {
  uint64_t files = 0;
  uint64_t total_size = 0;
  uint64_t terms = 0;
  uint64_t postings = 0;
  uint64_t roots = 0;
  uint64_t db_bytes = 0;
  int schema_version = 0;
  std::vector<std::pair<std::string, uint64_t>> top_extensions;
};

struct RemoveResult {
  uint64_t files_removed = 0;
  uint64_t roots_removed = 0;
};

}  // namespace filesearch
