#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "database/index_store.hpp"

namespace filesearch {

struct IndexOptions {
  bool include_hidden = false;     // index dot-files and descend into dot-directories
  bool follow_symlinks = false;    // descend into symlinked directories (cycle-safe)
  std::vector<std::string> excludes;  // glob patterns matched against entry names
  size_t batch_size = 5000;        // writes per database transaction
};

struct IndexReport {
  uint64_t files_seen = 0;
  uint64_t added = 0;
  uint64_t updated = 0;
  uint64_t unchanged = 0;
  uint64_t removed = 0;
  uint64_t dirs_scanned = 0;
  uint64_t skipped_hidden = 0;
  uint64_t skipped_excluded = 0;
  uint64_t symlinks_skipped = 0;   // directory links not followed, plus broken links
  uint64_t unreadable_dirs = 0;
  uint64_t errors = 0;
  bool cancelled = false;
  double seconds = 0;
  std::vector<std::string> error_samples;  // first few problems, for display
};

using ProgressFn = std::function<void(const IndexReport&)>;
using CancelFn = std::function<bool()>;

/// Walks a directory tree and keeps the IndexStore in sync with it.
///
/// Re-running on an already indexed root is incremental: files whose size and
/// modification time are unchanged are skipped, new and changed files are
/// (re)indexed, and entries for files that disappeared are removed. Entries
/// below directories that could not be read are never removed.
class Indexer {
 public:
  explicit Indexer(IndexStore& store) : store_(store) {}

  /// Throws Error when `root` is not a readable directory.
  IndexReport index(const std::filesystem::path& root, const IndexOptions& options,
                    const ProgressFn& progress = {}, const CancelFn& cancelled = {});

 private:
  IndexStore& store_;
};

}  // namespace filesearch
