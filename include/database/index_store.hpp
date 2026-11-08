#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "database/types.hpp"

namespace filesearch {

class Connection;

enum class OpenMode {
  CreateIfMissing,  // create the database (and its directory) on first use
  MustExist         // throw MissingDatabaseError when there is no database
};

/// Persistent storage: file metadata, the inverted index (postings) and the
/// list of indexed roots, all in one SQLite database.
///
/// The store never touches the files it describes.
class IndexStore {
 public:
  static constexpr int kSchemaVersion = 1;

  /// `db_path` may be ":memory:" (used by tests).
  IndexStore(const std::filesystem::path& db_path, OpenMode mode);
  ~IndexStore();
  IndexStore(const IndexStore&) = delete;
  IndexStore& operator=(const IndexStore&) = delete;

  const std::filesystem::path& path() const { return path_; }

  // --- transactions (used to batch many writes) ---------------------------
  void begin();
  void commit();
  void rollback() noexcept;
  bool in_transaction() const { return in_txn_; }

  // --- files ---------------------------------------------------------------
  std::optional<FileRow> find_file(const std::string& path);
  int64_t insert_file(const FileMeta& meta, int ntok);
  void update_file(int64_t id, const FileMeta& meta, int ntok);
  /// Replaces all postings of a file.
  void set_postings(int64_t file_id, const std::vector<TermCount>& terms);
  /// Deletes a file row together with its postings.
  void delete_file(int64_t id);
  /// Calls fn(id, path) for every file strictly below `dir`.
  void for_each_file_under(const std::string& dir,
                           const std::function<void(int64_t, const std::string&)>& fn);
  uint64_t file_count();

  // --- search support ------------------------------------------------------
  /// Vocabulary terms (with document frequency) that start with `token`
  /// (and, if `allow_substring`, that merely contain it).
  std::vector<std::pair<std::string, uint32_t>> expand_vocabulary(const std::string& token,
                                                                  bool allow_substring);
  uint32_t term_df(const std::string& term);
  void for_each_posting(const std::string& term,
                        const std::function<void(int64_t, uint32_t, uint32_t)>& fn);
  /// Fetches the given files, applying `filter`. Order is unspecified.
  std::vector<FileRow> fetch_files(const std::vector<int64_t>& ids, const FileFilter& filter);
  std::vector<FileRow> find_by_name(const std::string& name_lower, const FileFilter& filter);
  /// Pure filter query (no text). limit == 0 means unlimited.
  std::vector<FileRow> query_files(const FileFilter& filter, SortKey sort, bool reverse,
                                   size_t limit);
  uint64_t count_files(const FileFilter& filter);

  // --- roots ---------------------------------------------------------------
  void upsert_root(const RootInfo& root);
  std::vector<RootInfo> list_roots();

  // --- maintenance ---------------------------------------------------------
  /// Removes every file (and postings) below `dir` and every root at or below
  /// it, in one transaction.
  RemoveResult remove_directory(const std::string& dir);
  /// Deletes all files and postings but keeps the roots. One transaction.
  void clear_index();
  void prune_terms();
  void vacuum();
  void checkpoint() noexcept;
  StoreStats stats();
  /// Runs PRAGMA integrity_check; returns "ok" or a description of the problem.
  std::string integrity_check();

 private:
  struct Statements;
  void initialise(bool memory);

  std::filesystem::path path_;
  std::unique_ptr<Connection> conn_;
  std::unique_ptr<Statements> st_;
  bool in_txn_ = false;
};

}  // namespace filesearch
