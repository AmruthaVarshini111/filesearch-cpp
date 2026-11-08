#include "database/sqlite_wrapper.hpp"

#include "utils/error.hpp"

namespace filesearch {

void throw_sqlite_error(int rc, const std::string& detail) {
  const int primary = rc & 0xFF;
  if (primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB) throw CorruptDatabaseError(detail);
  throw DatabaseError(detail);
}

Connection::Connection(const std::string& path, bool create) {
  int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX;
  if (create) flags |= SQLITE_OPEN_CREATE;
  const int rc = sqlite3_open_v2(path.c_str(), &db_, flags, nullptr);
  if (rc != SQLITE_OK) {
    std::string detail = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw_sqlite_error(rc, "cannot open database '" + path + "': " + detail);
  }
}

Connection::~Connection() {
  if (db_) sqlite3_close(db_);
}

void Connection::exec(const std::string& sql) {
  char* msg = nullptr;
  const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &msg);
  if (rc != SQLITE_OK) {
    std::string detail = msg ? msg : sqlite3_errstr(rc);
    sqlite3_free(msg);
    throw_sqlite_error(rc, detail);
  }
}

int64_t Connection::scalar_int(const std::string& sql) {
  Statement st(*this, sql);
  return st.step() ? st.int_at(0) : 0;
}

Statement::Statement(Connection& conn, const std::string& sql) : db_(conn.handle()) {
  const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr);
  if (rc != SQLITE_OK) throw_sqlite_error(rc, std::string(sqlite3_errmsg(db_)) + " [" + sql + "]");
}

Statement::~Statement() {
  if (stmt_) sqlite3_finalize(stmt_);
}

Statement& Statement::bind_int(int idx, int64_t v) {
  sqlite3_bind_int64(stmt_, idx, v);
  return *this;
}

Statement& Statement::bind_text(int idx, const std::string& v) {
  sqlite3_bind_text(stmt_, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
  return *this;
}

Statement& Statement::bind_null(int idx) {
  sqlite3_bind_null(stmt_, idx);
  return *this;
}

bool Statement::step() {
  const int rc = sqlite3_step(stmt_);
  if (rc == SQLITE_ROW) return true;
  if (rc == SQLITE_DONE) return false;
  const std::string detail = sqlite3_errmsg(db_);
  sqlite3_reset(stmt_);
  throw_sqlite_error(rc, detail);
}

void Statement::run() {
  while (step()) {
  }
  reset();
}

void Statement::reset() {
  sqlite3_reset(stmt_);
  sqlite3_clear_bindings(stmt_);
}

int64_t Statement::int_at(int col) const { return sqlite3_column_int64(stmt_, col); }

std::string Statement::text_at(int col) const {
  const unsigned char* p = sqlite3_column_text(stmt_, col);
  const int n = sqlite3_column_bytes(stmt_, col);
  return p ? std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(n)) : std::string();
}

}  // namespace filesearch
