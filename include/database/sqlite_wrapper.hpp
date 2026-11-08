#pragma once
#include <sqlite3.h>

#include <cstdint>
#include <string>

namespace filesearch {

/// Throws CorruptDatabaseError for SQLITE_CORRUPT / SQLITE_NOTADB, otherwise
/// DatabaseError.
[[noreturn]] void throw_sqlite_error(int rc, const std::string& detail);

/// RAII owner of a sqlite3* handle.
class Connection {
 public:
  Connection(const std::string& path, bool create);
  ~Connection();
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  sqlite3* handle() const { return db_; }
  void exec(const std::string& sql);
  int64_t scalar_int(const std::string& sql);

 private:
  sqlite3* db_ = nullptr;
};

/// RAII prepared statement. Parameters are 1-based like in the SQLite API.
class Statement {
 public:
  Statement(Connection& conn, const std::string& sql);
  ~Statement();
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;

  Statement& bind_int(int idx, int64_t v);
  Statement& bind_text(int idx, const std::string& v);
  Statement& bind_null(int idx);

  /// Returns true when a row is available, false when finished.
  bool step();
  /// Runs a statement that returns no rows, then resets it.
  void run();
  void reset();

  int64_t int_at(int col) const;
  std::string text_at(int col) const;

 private:
  sqlite3* db_;
  sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace filesearch
