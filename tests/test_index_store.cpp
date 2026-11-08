#include <gtest/gtest.h>
#include <sqlite3.h>

#include "database/index_store.hpp"
#include "search/term_extractor.hpp"
#include "test_helpers.hpp"
#include "utils/error.hpp"

using namespace filesearch;
using namespace filesearch::testing;

namespace {

FileMeta meta(const std::string& path, uint64_t size = 10, int64_t mtime = 1000) {
  FileMeta m;
  m.path = path;
  m.name = fs::path(path).filename().string();
  const auto ext = fs::path(path).extension().string();
  m.ext = ext.empty() ? "" : ext.substr(1);
  m.size = size;
  m.mtime = mtime;
  return m;
}

int64_t add(IndexStore& s, const FileMeta& m) {
  const auto doc = extract_terms(m.name, fs::path(m.path).parent_path().string());
  const int64_t id = s.insert_file(m, doc.name_token_count);
  s.set_postings(id, doc.terms);
  return id;
}

IndexStore memory_store() { return IndexStore(":memory:", OpenMode::CreateIfMissing); }

}  // namespace

TEST(IndexStore, InsertFindUpdateDelete) {
  auto s = memory_store();
  const int64_t id = add(s, meta("/a/b/report.pdf", 42, 7));
  auto row = s.find_file("/a/b/report.pdf");
  ASSERT_TRUE(row);
  EXPECT_EQ(row->id, id);
  EXPECT_EQ(row->name, "report.pdf");
  EXPECT_EQ(row->ext, "pdf");
  EXPECT_EQ(row->size, 42u);
  EXPECT_EQ(row->mtime, 7);

  auto changed = meta("/a/b/report.pdf", 99, 8);
  s.update_file(id, changed, 2);
  EXPECT_EQ(s.find_file("/a/b/report.pdf")->size, 99u);
  EXPECT_EQ(s.file_count(), 1u);

  s.delete_file(id);
  EXPECT_FALSE(s.find_file("/a/b/report.pdf"));
  EXPECT_EQ(s.file_count(), 0u);
}

TEST(IndexStore, DuplicatePathsAreRejectedByTheSchema) {
  auto s = memory_store();
  add(s, meta("/a/x.txt"));
  EXPECT_THROW(s.insert_file(meta("/a/x.txt"), 1), DatabaseError);
}

TEST(IndexStore, DocumentFrequencyIsMaintainedByTriggers) {
  auto s = memory_store();
  const int64_t a = add(s, meta("/docs/learning-a.txt"));
  add(s, meta("/docs/learning-b.txt"));
  EXPECT_EQ(s.term_df("learning"), 2u);
  EXPECT_EQ(s.term_df("docs"), 2u);
  EXPECT_EQ(s.term_df("a"), 1u);
  s.delete_file(a);
  EXPECT_EQ(s.term_df("learning"), 1u);
  EXPECT_EQ(s.term_df("a"), 0u);
  s.set_postings(s.find_file("/docs/learning-b.txt")->id, {});  // replacing postings updates df too
  EXPECT_EQ(s.term_df("learning"), 0u);
}

TEST(IndexStore, PostingsRoundTrip) {
  auto s = memory_store();
  const int64_t id = add(s, meta("/learning/learning.txt"));
  int seen = 0;
  s.for_each_posting("learning", [&](int64_t doc, uint32_t tfn, uint32_t tfp) {
    EXPECT_EQ(doc, id);
    EXPECT_EQ(tfn, 1u);
    EXPECT_EQ(tfp, 1u);
    ++seen;
  });
  EXPECT_EQ(seen, 1);
}

TEST(IndexStore, VocabularyExpansion) {
  auto s = memory_store();
  add(s, meta("/x/learning.txt"));
  add(s, meta("/x/relearn.txt"));
  const auto prefix = s.expand_vocabulary("learn", false);
  ASSERT_EQ(prefix.size(), 1u);
  EXPECT_EQ(prefix[0].first, "learning");
  const auto sub = s.expand_vocabulary("learn", true);
  EXPECT_EQ(sub.size(), 2u);
}

TEST(IndexStore, RemoveDirectoryRemovesFilesPostingsAndRoots) {
  auto s = memory_store();
  add(s, meta("/keep/a.txt"));
  add(s, meta("/gone/b.txt"));
  add(s, meta("/gone/sub/c.txt"));
  add(s, meta("/goneaway/d.txt"));  // shares a prefix but is a different directory
  RootInfo r;
  r.path = "/gone";
  s.upsert_root(r);
  r.path = "/gone/sub";
  s.upsert_root(r);
  const auto res = s.remove_directory("/gone");
  EXPECT_EQ(res.files_removed, 2u);
  EXPECT_EQ(res.roots_removed, 2u);
  EXPECT_EQ(s.file_count(), 2u);
  EXPECT_TRUE(s.find_file("/goneaway/d.txt"));
  EXPECT_EQ(s.term_df("sub"), 0u);
  EXPECT_TRUE(s.list_roots().empty());
}

TEST(IndexStore, ClearIndexKeepsRootsAndRestoresTriggers) {
  auto s = memory_store();
  add(s, meta("/x/a.txt"));
  RootInfo r;
  r.path = "/x";
  r.include_hidden = true;
  r.excludes = {"*.log", "node_modules"};
  s.upsert_root(r);
  s.clear_index();
  EXPECT_EQ(s.file_count(), 0u);
  EXPECT_EQ(s.stats().postings, 0u);
  const auto roots = s.list_roots();
  ASSERT_EQ(roots.size(), 1u);
  EXPECT_TRUE(roots[0].include_hidden);
  EXPECT_EQ(roots[0].excludes, (std::vector<std::string>{"*.log", "node_modules"}));
  add(s, meta("/x/b.txt"));  // triggers must work again
  EXPECT_EQ(s.term_df("txt"), 1u);
}

TEST(IndexStore, FiltersApplyInSql) {
  auto s = memory_store();
  const int64_t small_pdf = add(s, meta("/docs/small.pdf", 100, 1000));
  const int64_t big_pdf = add(s, meta("/docs/big.pdf", 5000000, 2000));
  const int64_t txt = add(s, meta("/other/note.txt", 100, 3000));
  const std::vector<int64_t> all{small_pdf, big_pdf, txt};

  FileFilter f;
  f.extensions = {"pdf"};
  EXPECT_EQ(s.fetch_files(all, f).size(), 2u);
  f = {};
  f.directory = "/docs";
  EXPECT_EQ(s.fetch_files(all, f).size(), 2u);
  f = {};
  f.min_size = 1000;
  ASSERT_EQ(s.fetch_files(all, f).size(), 1u);
  EXPECT_EQ(s.fetch_files(all, f)[0].id, big_pdf);
  f = {};
  f.max_size = 100;
  EXPECT_EQ(s.fetch_files(all, f).size(), 2u);
  f = {};
  f.modified_after = 1500;
  f.modified_before = 2500;
  ASSERT_EQ(s.fetch_files(all, f).size(), 1u);
  f = {};
  f.directory = "/doc";  // a directory filter never matches a longer sibling name
  EXPECT_TRUE(s.fetch_files(all, f).empty());
}

TEST(IndexStore, DirectoryFilterTreatsLikeWildcardsLiterally) {
  auto s = memory_store();
  const int64_t a = add(s, meta("/data/100%_done/a.txt"));
  const int64_t b = add(s, meta("/data/100x_done/b.txt"));
  FileFilter f;
  f.directory = "/data/100%_done";
  const auto rows = s.fetch_files({a, b}, f);
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].id, a);
}

TEST(IndexStore, FetchHandlesMoreIdsThanOneChunk) {
  auto s = memory_store();
  std::vector<int64_t> ids;
  s.begin();
  for (int i = 0; i < 1300; ++i) ids.push_back(add(s, meta("/bulk/file" + std::to_string(i) + ".txt")));
  s.commit();
  EXPECT_EQ(s.fetch_files(ids, {}).size(), 1300u);
}

TEST(IndexStore, QueryFilesSortsAndLimits) {
  auto s = memory_store();
  add(s, meta("/d/b.txt", 20, 300));
  add(s, meta("/d/a.txt", 10, 100));
  add(s, meta("/d/c.txt", 30, 200));
  EXPECT_EQ(s.query_files({}, SortKey::Size, false, 0)[0].name, "c.txt");
  EXPECT_EQ(s.query_files({}, SortKey::Size, true, 0)[0].name, "a.txt");
  EXPECT_EQ(s.query_files({}, SortKey::Date, false, 0)[0].name, "b.txt");
  EXPECT_EQ(s.query_files({}, SortKey::Name, false, 0)[0].name, "a.txt");
  EXPECT_EQ(s.query_files({}, SortKey::Name, false, 2).size(), 2u);
  EXPECT_EQ(s.count_files({}), 3u);
}

TEST(IndexStore, StatsReportCounts) {
  auto s = memory_store();
  add(s, meta("/d/a.txt", 10));
  add(s, meta("/d/b.txt", 30));
  add(s, meta("/d/c.pdf", 60));
  const auto st = s.stats();
  EXPECT_EQ(st.files, 3u);
  EXPECT_EQ(st.total_size, 100u);
  EXPECT_GT(st.terms, 0u);
  EXPECT_GT(st.postings, 0u);
  EXPECT_EQ(st.schema_version, IndexStore::kSchemaVersion);
  ASSERT_FALSE(st.top_extensions.empty());
  EXPECT_EQ(st.top_extensions[0].first, "txt");
  EXPECT_EQ(st.top_extensions[0].second, 2u);
}

TEST(IndexStore, RollbackDiscardsUncommittedWrites) {
  auto s = memory_store();
  s.begin();
  add(s, meta("/d/a.txt"));
  s.rollback();
  EXPECT_EQ(s.file_count(), 0u);
}

TEST(IndexStore, PersistsAcrossReopen) {
  TempDir tmp;
  const auto db = tmp.path() / "sub" / "index.db";  // parent directory is created on demand
  {
    IndexStore s(db, OpenMode::CreateIfMissing);
    add(s, meta("/d/persist.txt"));
    RootInfo r;
    r.path = "/d";
    s.upsert_root(r);
  }
  IndexStore again(db, OpenMode::MustExist);
  EXPECT_TRUE(again.find_file("/d/persist.txt"));
  EXPECT_EQ(again.term_df("persist"), 1u);
  EXPECT_EQ(again.list_roots().size(), 1u);
  EXPECT_EQ(again.integrity_check(), "ok");
}

TEST(IndexStore, MissingDatabaseIsReportedNotCreated) {
  TempDir tmp;
  const auto db = tmp.path() / "nope.db";
  EXPECT_THROW(IndexStore(db, OpenMode::MustExist), MissingDatabaseError);
  EXPECT_FALSE(fs::exists(db));
}

TEST(IndexStore, GarbageFileIsReportedAsCorrupt) {
  TempDir tmp;
  const auto db = tmp.path() / "bad.db";
  write_file(db, std::string(4096, 'z') + "this is definitely not an sqlite database");
  EXPECT_THROW(IndexStore(db, OpenMode::MustExist), CorruptDatabaseError);
  EXPECT_THROW(IndexStore(db, OpenMode::CreateIfMissing), CorruptDatabaseError);
}

TEST(IndexStore, TruncatedDatabaseIsReportedAsCorrupt) {
  TempDir tmp;
  const auto db = tmp.path() / "trunc.db";
  {
    IndexStore s(db, OpenMode::CreateIfMissing);
    s.begin();
    for (int i = 0; i < 200; ++i) add(s, meta("/d/file" + std::to_string(i) + ".txt"));
    s.commit();
    s.checkpoint();
  }
  fs::resize_file(db, 5000);  // cut the file in the middle of the b-trees
  try {
    IndexStore s(db, OpenMode::MustExist);
    s.stats();
    s.integrity_check();
    FAIL() << "expected a corruption error";
  } catch (const CorruptDatabaseError&) {
  } catch (const DatabaseError&) {
    // acceptable: SQLite may report a different low-level error for truncation
  }
}

TEST(IndexStore, ForeignSqliteDatabaseIsRejected) {
  TempDir tmp;
  const auto db = tmp.path() / "other.db";
  sqlite3* raw = nullptr;
  ASSERT_EQ(sqlite3_open(db.c_str(), &raw), SQLITE_OK);
  ASSERT_EQ(sqlite3_exec(raw, "CREATE TABLE unrelated(x)", nullptr, nullptr, nullptr), SQLITE_OK);
  sqlite3_close(raw);
  EXPECT_THROW(IndexStore(db, OpenMode::MustExist), CorruptDatabaseError);
}

TEST(IndexStore, DirectoryAsDatabasePathIsAnError) {
  TempDir tmp;
  EXPECT_THROW(IndexStore(tmp.path(), OpenMode::CreateIfMissing), DatabaseError);
}
