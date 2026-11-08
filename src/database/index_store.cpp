#include "database/index_store.hpp"

#include <algorithm>
#include <chrono>
#include <system_error>

#include "database/sqlite_wrapper.hpp"
#include "utils/error.hpp"
#include "utils/path_utils.hpp"
#include "utils/string_utils.hpp"

namespace filesearch {
namespace fs = std::filesystem;

namespace {

// Postings use WITHOUT ROWID so the (term, file_id) primary key *is* the table:
// looking up all postings of a term is a single range scan.
// `terms` keeps the document frequency of each term; triggers maintain it so
// callers never have to.
constexpr const char* kTableSql = R"SQL(
CREATE TABLE IF NOT EXISTS roots (
  path TEXT PRIMARY KEY,
  include_hidden INTEGER NOT NULL,
  follow_symlinks INTEGER NOT NULL,
  excludes TEXT NOT NULL,
  last_indexed INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS files (
  id INTEGER PRIMARY KEY,
  path TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  name_lower TEXT NOT NULL,
  ext TEXT NOT NULL,
  size INTEGER NOT NULL,
  mtime INTEGER NOT NULL,
  ntok INTEGER NOT NULL,
  indexed_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_files_name ON files(name_lower);
CREATE INDEX IF NOT EXISTS idx_files_ext ON files(ext);
CREATE INDEX IF NOT EXISTS idx_files_size ON files(size);
CREATE INDEX IF NOT EXISTS idx_files_mtime ON files(mtime);
CREATE TABLE IF NOT EXISTS postings (
  term TEXT NOT NULL,
  file_id INTEGER NOT NULL,
  tf_name INTEGER NOT NULL,
  tf_path INTEGER NOT NULL,
  PRIMARY KEY (term, file_id)
) WITHOUT ROWID;
CREATE INDEX IF NOT EXISTS idx_postings_file ON postings(file_id);
CREATE TABLE IF NOT EXISTS terms (
  term TEXT PRIMARY KEY,
  df INTEGER NOT NULL
) WITHOUT ROWID;
)SQL";

constexpr const char* kTriggerSql = R"SQL(
CREATE TRIGGER IF NOT EXISTS trg_postings_ai AFTER INSERT ON postings BEGIN
  INSERT INTO terms(term, df) VALUES (NEW.term, 1)
    ON CONFLICT(term) DO UPDATE SET df = df + 1;
END;
CREATE TRIGGER IF NOT EXISTS trg_postings_ad AFTER DELETE ON postings BEGIN
  UPDATE terms SET df = df - 1 WHERE term = OLD.term;
END;
)SQL";

constexpr const char* kSelectFile = "SELECT id, path, name, ext, size, mtime, ntok FROM files";

int64_t now_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

FileRow row_from(Statement& st) {
  FileRow r;
  r.id = st.int_at(0);
  r.path = st.text_at(1);
  r.name = st.text_at(2);
  r.ext = st.text_at(3);
  r.size = static_cast<uint64_t>(st.int_at(4));
  r.mtime = st.int_at(5);
  r.ntok = static_cast<int>(st.int_at(6));
  return r;
}

struct BindValue {
  bool is_text;
  int64_t i;
  std::string s;
};

// Builds " AND ..." conditions for a FileFilter with positional '?' parameters.
std::string filter_clause(const FileFilter& f, std::vector<BindValue>& binds) {
  std::string sql;
  if (!f.extensions.empty()) {
    sql += " AND ext IN (";
    for (size_t i = 0; i < f.extensions.size(); ++i) {
      sql += i ? ",?" : "?";
      binds.push_back({true, 0, f.extensions[i]});
    }
    sql += ")";
  }
  if (f.directory) {
    sql += " AND path LIKE ? ESCAPE '\\'";
    binds.push_back({true, 0, strutil::escape_like(pathutil::dir_prefix(*f.directory)) + "%"});
  }
  if (f.min_size) {
    sql += " AND size >= ?";
    binds.push_back({false, static_cast<int64_t>(*f.min_size), {}});
  }
  if (f.max_size) {
    sql += " AND size <= ?";
    binds.push_back({false, static_cast<int64_t>(*f.max_size), {}});
  }
  if (f.modified_after) {
    sql += " AND mtime >= ?";
    binds.push_back({false, *f.modified_after, {}});
  }
  if (f.modified_before) {
    sql += " AND mtime <= ?";
    binds.push_back({false, *f.modified_before, {}});
  }
  return sql;
}

void bind_all(Statement& st, const std::vector<BindValue>& binds, int first_index) {
  int idx = first_index;
  for (const auto& b : binds) {
    if (b.is_text)
      st.bind_text(idx++, b.s);
    else
      st.bind_int(idx++, b.i);
  }
}

}  // namespace

struct IndexStore::Statements {
  Statement find_file, insert_file, update_file, delete_file, delete_postings, insert_posting,
      count_files, term_df, postings_for_term, vocab_substring, vocab_prefix, upsert_root;

  explicit Statements(Connection& c)
      : find_file(c, std::string(kSelectFile) + " WHERE path = ?1"),
        insert_file(c,
                    "INSERT INTO files(path, name, name_lower, ext, size, mtime, ntok, indexed_at) "
                    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)"),
        update_file(c,
                    "UPDATE files SET name = ?2, name_lower = ?3, ext = ?4, size = ?5, "
                    "mtime = ?6, ntok = ?7, indexed_at = ?8 WHERE id = ?1"),
        delete_file(c, "DELETE FROM files WHERE id = ?1"),
        delete_postings(c, "DELETE FROM postings WHERE file_id = ?1"),
        insert_posting(c,
                       "INSERT INTO postings(term, file_id, tf_name, tf_path) VALUES (?1, ?2, ?3, ?4)"),
        count_files(c, "SELECT COUNT(*) FROM files"),
        term_df(c, "SELECT df FROM terms WHERE term = ?1"),
        postings_for_term(c, "SELECT file_id, tf_name, tf_path FROM postings WHERE term = ?1"),
        vocab_substring(c, "SELECT term, df FROM terms WHERE df > 0 AND instr(term, ?1) > 0"),
        vocab_prefix(c, "SELECT term, df FROM terms WHERE df > 0 AND term >= ?1 AND term < ?2"),
        upsert_root(c,
                    "INSERT INTO roots(path, include_hidden, follow_symlinks, excludes, last_indexed) "
                    "VALUES (?1, ?2, ?3, ?4, ?5) "
                    "ON CONFLICT(path) DO UPDATE SET include_hidden = excluded.include_hidden, "
                    "follow_symlinks = excluded.follow_symlinks, excludes = excluded.excludes, "
                    "last_indexed = excluded.last_indexed") {}
};

IndexStore::IndexStore(const fs::path& db_path, OpenMode mode) : path_(db_path) {
  const bool memory = db_path == ":memory:";
  if (!memory) {
    std::error_code ec;
    const bool exists = fs::exists(db_path, ec);
    if (!exists && mode == OpenMode::MustExist)
      throw MissingDatabaseError("no index found at " + db_path.string());
    if (exists && fs::is_directory(db_path, ec))
      throw DatabaseError("database path is a directory: " + db_path.string());
    if (!exists) {
      const fs::path parent = db_path.parent_path();
      if (!parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec)
          throw DatabaseError("cannot create directory " + parent.string() + ": " + ec.message());
      }
    }
  }
  conn_ = std::make_unique<Connection>(db_path.string(), mode == OpenMode::CreateIfMissing || memory);
  initialise(memory);
  st_ = std::make_unique<Statements>(*conn_);
}

IndexStore::~IndexStore() {
  if (in_txn_) rollback();
  st_.reset();  // finalize statements before closing the connection
  conn_.reset();
}

void IndexStore::initialise(bool memory) {
  conn_->exec("PRAGMA busy_timeout = 5000");
  // The first statement that reads the file reports SQLITE_NOTADB / CORRUPT
  // for files that are not databases.
  if (!memory) conn_->exec("PRAGMA journal_mode = WAL");
  conn_->exec("PRAGMA synchronous = NORMAL");
  conn_->exec("PRAGMA temp_store = MEMORY");
  conn_->exec("PRAGMA cache_size = -65536");

  const int64_t version = conn_->scalar_int("PRAGMA user_version");
  const int64_t objects = conn_->scalar_int("SELECT COUNT(*) FROM sqlite_master");
  if (version == 0) {
    if (objects > 0)
      throw CorruptDatabaseError("this SQLite file is not a FileSearch index");
    conn_->exec("BEGIN IMMEDIATE");
    try {
      conn_->exec(kTableSql);
      conn_->exec(kTriggerSql);
      conn_->exec("PRAGMA user_version = " + std::to_string(kSchemaVersion));
      conn_->exec("COMMIT");
    } catch (...) {
      try { conn_->exec("ROLLBACK"); } catch (...) {}
      throw;
    }
  } else if (version != kSchemaVersion) {
    throw CorruptDatabaseError("unsupported index schema version " + std::to_string(version));
  }
}

void IndexStore::begin() {
  if (in_txn_) return;
  conn_->exec("BEGIN IMMEDIATE");
  in_txn_ = true;
}

void IndexStore::commit() {
  if (!in_txn_) return;
  conn_->exec("COMMIT");
  in_txn_ = false;
}

void IndexStore::rollback() noexcept {
  if (!in_txn_) return;
  try { conn_->exec("ROLLBACK"); } catch (...) {}
  in_txn_ = false;
}

std::optional<FileRow> IndexStore::find_file(const std::string& path) {
  auto& st = st_->find_file;
  st.bind_text(1, path);
  std::optional<FileRow> out;
  if (st.step()) out = row_from(st);
  st.reset();
  return out;
}

int64_t IndexStore::insert_file(const FileMeta& m, int ntok) {
  auto& st = st_->insert_file;
  st.bind_text(1, m.path).bind_text(2, m.name).bind_text(3, strutil::to_lower(m.name));
  st.bind_text(4, m.ext).bind_int(5, static_cast<int64_t>(m.size)).bind_int(6, m.mtime);
  st.bind_int(7, ntok).bind_int(8, now_seconds());
  st.run();
  return sqlite3_last_insert_rowid(conn_->handle());
}

void IndexStore::update_file(int64_t id, const FileMeta& m, int ntok) {
  auto& st = st_->update_file;
  st.bind_int(1, id).bind_text(2, m.name).bind_text(3, strutil::to_lower(m.name));
  st.bind_text(4, m.ext).bind_int(5, static_cast<int64_t>(m.size)).bind_int(6, m.mtime);
  st.bind_int(7, ntok).bind_int(8, now_seconds());
  st.run();
}

void IndexStore::set_postings(int64_t file_id, const std::vector<TermCount>& terms) {
  st_->delete_postings.bind_int(1, file_id).run();
  for (const auto& t : terms) {
    auto& st = st_->insert_posting;
    st.bind_text(1, t.term).bind_int(2, file_id);
    st.bind_int(3, t.tf_name).bind_int(4, t.tf_path);
    st.run();
  }
}

void IndexStore::delete_file(int64_t id) {
  st_->delete_postings.bind_int(1, id).run();
  st_->delete_file.bind_int(1, id).run();
}

void IndexStore::for_each_file_under(const std::string& dir,
                                     const std::function<void(int64_t, const std::string&)>& fn) {
  const std::string lo = pathutil::dir_prefix(dir);
  Statement st(*conn_, "SELECT id, path FROM files WHERE path >= ?1 AND path < ?2");
  st.bind_text(1, lo).bind_text(2, pathutil::prefix_upper_bound(lo));
  while (st.step()) fn(st.int_at(0), st.text_at(1));
}

uint64_t IndexStore::file_count() {
  auto& st = st_->count_files;
  uint64_t n = st.step() ? static_cast<uint64_t>(st.int_at(0)) : 0;
  st.reset();
  return n;
}

std::vector<std::pair<std::string, uint32_t>> IndexStore::expand_vocabulary(
    const std::string& token, bool allow_substring) {
  std::vector<std::pair<std::string, uint32_t>> out;
  Statement& st = [&]() -> Statement& {
    if (allow_substring) {
      st_->vocab_substring.bind_text(1, token);
      return st_->vocab_substring;
    }
    st_->vocab_prefix.bind_text(1, token).bind_text(2, pathutil::prefix_upper_bound(token));
    return st_->vocab_prefix;
  }();
  while (st.step()) out.emplace_back(st.text_at(0), static_cast<uint32_t>(st.int_at(1)));
  st.reset();
  return out;
}

uint32_t IndexStore::term_df(const std::string& term) {
  auto& st = st_->term_df;
  st.bind_text(1, term);
  uint32_t df = st.step() ? static_cast<uint32_t>(std::max<int64_t>(0, st.int_at(0))) : 0;
  st.reset();
  return df;
}

void IndexStore::for_each_posting(const std::string& term,
                                  const std::function<void(int64_t, uint32_t, uint32_t)>& fn) {
  auto& st = st_->postings_for_term;
  st.bind_text(1, term);
  while (st.step())
    fn(st.int_at(0), static_cast<uint32_t>(st.int_at(1)), static_cast<uint32_t>(st.int_at(2)));
  st.reset();
}

std::vector<FileRow> IndexStore::fetch_files(const std::vector<int64_t>& ids,
                                             const FileFilter& filter) {
  std::vector<FileRow> out;
  constexpr size_t kChunk = 500;
  std::vector<BindValue> binds;
  const std::string clause = filter_clause(filter, binds);
  for (size_t off = 0; off < ids.size(); off += kChunk) {
    const size_t n = std::min(kChunk, ids.size() - off);
    std::string sql = std::string(kSelectFile) + " WHERE id IN (?";
    for (size_t i = 1; i < n; ++i) sql += ",?";
    sql += ")" + clause;
    Statement st(*conn_, sql);
    for (size_t i = 0; i < n; ++i) st.bind_int(static_cast<int>(i) + 1, ids[off + i]);
    bind_all(st, binds, static_cast<int>(n) + 1);
    while (st.step()) out.push_back(row_from(st));
  }
  return out;
}

std::vector<FileRow> IndexStore::find_by_name(const std::string& name_lower,
                                              const FileFilter& filter) {
  std::vector<BindValue> binds;
  Statement st(*conn_,
               std::string(kSelectFile) + " WHERE name_lower = ?" + filter_clause(filter, binds));
  st.bind_text(1, name_lower);
  bind_all(st, binds, 2);
  std::vector<FileRow> out;
  while (st.step()) out.push_back(row_from(st));
  return out;
}

std::vector<FileRow> IndexStore::query_files(const FileFilter& filter, SortKey sort, bool reverse,
                                             size_t limit) {
  std::vector<BindValue> binds;
  std::string sql = std::string(kSelectFile) + " WHERE 1=1" + filter_clause(filter, binds);
  const char* asc = reverse ? "DESC" : "ASC";
  const char* desc = reverse ? "ASC" : "DESC";
  switch (sort) {
    case SortKey::Size: sql += std::string(" ORDER BY size ") + desc + ", path ASC"; break;
    case SortKey::Date: sql += std::string(" ORDER BY mtime ") + desc + ", path ASC"; break;
    default: sql += std::string(" ORDER BY name_lower ") + asc + ", path ASC"; break;
  }
  sql += " LIMIT ?";
  Statement st(*conn_, sql);
  bind_all(st, binds, 1);
  st.bind_int(static_cast<int>(binds.size()) + 1, limit == 0 ? -1 : static_cast<int64_t>(limit));
  std::vector<FileRow> out;
  while (st.step()) out.push_back(row_from(st));
  return out;
}

uint64_t IndexStore::count_files(const FileFilter& filter) {
  std::vector<BindValue> binds;
  Statement st(*conn_, "SELECT COUNT(*) FROM files WHERE 1=1" + filter_clause(filter, binds));
  bind_all(st, binds, 1);
  return st.step() ? static_cast<uint64_t>(st.int_at(0)) : 0;
}

void IndexStore::upsert_root(const RootInfo& r) {
  auto& st = st_->upsert_root;
  st.bind_text(1, r.path).bind_int(2, r.include_hidden).bind_int(3, r.follow_symlinks);
  st.bind_text(4, strutil::join(r.excludes, "\n")).bind_int(5, r.last_indexed);
  st.run();
}

std::vector<RootInfo> IndexStore::list_roots() {
  Statement st(*conn_,
               "SELECT path, include_hidden, follow_symlinks, excludes, last_indexed FROM roots "
               "ORDER BY path");
  std::vector<RootInfo> out;
  while (st.step()) {
    RootInfo r;
    r.path = st.text_at(0);
    r.include_hidden = st.int_at(1) != 0;
    r.follow_symlinks = st.int_at(2) != 0;
    const std::string ex = st.text_at(3);
    if (!ex.empty()) r.excludes = strutil::split(ex, '\n');
    r.last_indexed = st.int_at(4);
    out.push_back(std::move(r));
  }
  return out;
}

RemoveResult IndexStore::remove_directory(const std::string& dir) {
  const bool own_txn = !in_txn_;
  if (own_txn) begin();
  RemoveResult res;
  try {
    const std::string lo = pathutil::dir_prefix(dir);
    const std::string hi = pathutil::prefix_upper_bound(lo);
    {
      Statement del_post(*conn_,
                         "DELETE FROM postings WHERE file_id IN "
                         "(SELECT id FROM files WHERE path >= ?1 AND path < ?2)");
      del_post.bind_text(1, lo).bind_text(2, hi).run();
      Statement del_files(*conn_, "DELETE FROM files WHERE path >= ?1 AND path < ?2");
      del_files.bind_text(1, lo).bind_text(2, hi).run();
      res.files_removed = static_cast<uint64_t>(sqlite3_changes(conn_->handle()));
      Statement del_roots(*conn_,
                          "DELETE FROM roots WHERE path = ?1 OR (path >= ?2 AND path < ?3)");
      del_roots.bind_text(1, dir).bind_text(2, lo).bind_text(3, hi).run();
      res.roots_removed = static_cast<uint64_t>(sqlite3_changes(conn_->handle()));
    }
    prune_terms();
    if (own_txn) commit();
  } catch (...) {
    if (own_txn) rollback();
    throw;
  }
  return res;
}

void IndexStore::clear_index() {
  const bool own_txn = !in_txn_;
  if (own_txn) begin();
  try {
    // Dropping the triggers first avoids one `terms` update per deleted row.
    conn_->exec("DROP TRIGGER IF EXISTS trg_postings_ai; DROP TRIGGER IF EXISTS trg_postings_ad;");
    conn_->exec("DELETE FROM postings; DELETE FROM terms; DELETE FROM files;");
    conn_->exec(kTriggerSql);
    if (own_txn) commit();
  } catch (...) {
    if (own_txn) rollback();
    throw;
  }
}

void IndexStore::prune_terms() { conn_->exec("DELETE FROM terms WHERE df <= 0"); }

void IndexStore::vacuum() {
  commit();
  conn_->exec("VACUUM");
}

void IndexStore::checkpoint() noexcept {
  try {
    if (!in_txn_ && path_ != ":memory:") conn_->exec("PRAGMA wal_checkpoint(TRUNCATE)");
  } catch (...) {
  }
}

StoreStats IndexStore::stats() {
  StoreStats s;
  s.files = file_count();
  s.total_size = static_cast<uint64_t>(conn_->scalar_int("SELECT COALESCE(SUM(size), 0) FROM files"));
  s.terms = static_cast<uint64_t>(conn_->scalar_int("SELECT COUNT(*) FROM terms WHERE df > 0"));
  s.postings = static_cast<uint64_t>(conn_->scalar_int("SELECT COUNT(*) FROM postings"));
  s.roots = static_cast<uint64_t>(conn_->scalar_int("SELECT COUNT(*) FROM roots"));
  s.schema_version = static_cast<int>(conn_->scalar_int("PRAGMA user_version"));
  {
    Statement st(*conn_,
                 "SELECT ext, COUNT(*) AS c FROM files GROUP BY ext ORDER BY c DESC, ext ASC LIMIT 5");
    while (st.step()) s.top_extensions.emplace_back(st.text_at(0), static_cast<uint64_t>(st.int_at(1)));
  }
  if (path_ != ":memory:") {
    std::error_code ec;
    auto sz = fs::file_size(path_, ec);
    if (!ec) s.db_bytes += sz;
    auto wal = fs::file_size(fs::path(path_.string() + "-wal"), ec);
    if (!ec) s.db_bytes += wal;
  }
  return s;
}

std::string IndexStore::integrity_check() {
  Statement st(*conn_, "PRAGMA integrity_check");
  std::vector<std::string> lines;
  while (st.step() && lines.size() < 5) lines.push_back(st.text_at(0));
  return strutil::join(lines, "; ");
}

}  // namespace filesearch
