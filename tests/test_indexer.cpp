#include <map>
#include <gtest/gtest.h>
#include <sys/stat.h>

#include "indexer/indexer.hpp"
#include "test_helpers.hpp"
#include "utils/error.hpp"

using namespace filesearch;
using namespace filesearch::testing;

namespace {
class IndexerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root = copy_sample_data(tmp.path());
    store = std::make_unique<IndexStore>(":memory:", OpenMode::CreateIfMissing);
  }
  IndexReport index(IndexOptions o = {}) { return Indexer(*store).index(root, o); }
  TempDir tmp;
  fs::path root;
  std::unique_ptr<IndexStore> store;
};
}  // namespace

TEST_F(IndexerTest, IndexesSampleTreeWithoutHiddenFiles) {
  const auto r = index();
  EXPECT_EQ(r.files_seen, 11u);
  EXPECT_EQ(r.added, 11u);
  EXPECT_EQ(r.skipped_hidden, 2u);  // .hidden/ and .config
  EXPECT_EQ(store->file_count(), 11u);
  EXPECT_FALSE(store->find_file((root / ".config").string()));
}

TEST_F(IndexerTest, HiddenFilesAreIncludedOnRequest) {
  IndexOptions o;
  o.include_hidden = true;
  index(o);
  EXPECT_EQ(store->file_count(), 13u);
  EXPECT_TRUE(store->find_file((root / ".hidden" / "secret.txt").string()));
}

TEST_F(IndexerTest, StoresMetadataAndAbsolutePaths) {
  write_bytes(root / "meta" / "Data.TAR.GZ", 123);
  set_mtime(root / "meta" / "Data.TAR.GZ", 1600000000);
  index();
  const auto row = store->find_file((root / "meta" / "Data.TAR.GZ").string());
  ASSERT_TRUE(row);
  EXPECT_EQ(row->size, 123u);
  EXPECT_EQ(row->mtime, 1600000000);
  EXPECT_EQ(row->ext, "gz");
  EXPECT_EQ(row->name, "Data.TAR.GZ");
  EXPECT_EQ(row->path.front(), '/');
}

TEST_F(IndexerTest, ReindexingNeverCreatesDuplicates) {
  index();
  const auto again = index();
  EXPECT_EQ(again.added, 0u);
  EXPECT_EQ(again.updated, 0u);
  EXPECT_EQ(again.unchanged, 11u);
  EXPECT_EQ(store->file_count(), 11u);
}

TEST_F(IndexerTest, IncrementalIndexingHandlesAddChangeAndDelete) {
  index();
  write_file(root / "documents" / "new-file.txt", "new");
  write_bytes(root / "code" / "main.cpp", 500);  // size changes
  fs::remove(root / "photos" / "vacation-2023.jpg");
  const auto r = index();
  EXPECT_EQ(r.added, 1u);
  EXPECT_EQ(r.updated, 1u);
  EXPECT_EQ(r.removed, 1u);
  EXPECT_EQ(r.unchanged, 9u);
  EXPECT_EQ(store->file_count(), 11u);
  EXPECT_EQ(store->term_df("vacation"), 0u);  // postings of the deleted file are gone
  EXPECT_EQ(store->term_df("new"), 1u);
  EXPECT_EQ(store->find_file((root / "code" / "main.cpp").string())->size, 500u);
}

TEST_F(IndexerTest, ModifiedTimeChangeTriggersUpdate) {
  index();
  set_mtime(root / "code" / "README.md", 1234567890);
  const auto r = index();
  EXPECT_EQ(r.updated, 1u);
  EXPECT_EQ(store->find_file((root / "code" / "README.md").string())->mtime, 1234567890);
}

TEST_F(IndexerTest, ExcludePatternsSkipFilesAndDirectories) {
  write_file(root / "node_modules" / "pkg" / "index.js");
  write_file(root / "documents" / "debug.log");
  IndexOptions o;
  o.excludes = {"node_modules", "*.log"};
  const auto r = index(o);
  EXPECT_EQ(r.skipped_excluded, 2u);
  EXPECT_FALSE(store->find_file((root / "documents" / "debug.log").string()));
  EXPECT_FALSE(store->find_file((root / "node_modules" / "pkg" / "index.js").string()));
}

TEST_F(IndexerTest, SymlinkedDirectoriesAreNotFollowedByDefault) {
  fs::create_directory_symlink(root / "documents", root / "docs-link");
  const auto r = index();
  EXPECT_EQ(r.files_seen, 11u);
  EXPECT_EQ(r.symlinks_skipped, 1u);
}

TEST_F(IndexerTest, SymlinkLoopsTerminateWhenFollowing) {
  fs::create_directory_symlink(root, root / "documents" / "loop");       // points at an ancestor
  fs::create_directory_symlink(root / "documents", root / "code" / "docs");  // alias, not a loop
  IndexOptions o;
  o.follow_symlinks = true;
  const auto r = index(o);  // must return rather than recurse forever
  EXPECT_EQ(r.errors, 0u);
  EXPECT_EQ(r.files_seen, 11u);  // each real directory is entered once; aliases add nothing
}

TEST_F(IndexerTest, BrokenSymlinksAreSkippedAndFileSymlinksAreIndexed) {
  fs::create_symlink(root / "does-not-exist", root / "broken");
  fs::create_symlink(root / "code" / "main.cpp", root / "main-link.cpp");
  const auto r = index();
  EXPECT_EQ(r.symlinks_skipped, 1u);
  EXPECT_EQ(r.errors, 0u);
  EXPECT_FALSE(store->find_file((root / "broken").string()));
  EXPECT_TRUE(store->find_file((root / "main-link.cpp").string()));
}

TEST_F(IndexerTest, UnreadableDirectoriesAreReportedNotFatal) {
  if (geteuid() == 0) GTEST_SKIP() << "permission checks do not apply to root";
  write_file(root / "locked" / "secret.txt");
  fs::permissions(root / "locked", fs::perms::none);
  const auto r = index();
  EXPECT_EQ(r.unreadable_dirs, 1u);
  EXPECT_GE(r.errors, 1u);
  ASSERT_FALSE(r.error_samples.empty());
  EXPECT_EQ(r.files_seen, 11u);  // everything else was still indexed
}

TEST_F(IndexerTest, EntriesBelowUnreadableDirectoriesAreKept) {
  if (geteuid() == 0) GTEST_SKIP() << "permission checks do not apply to root";
  write_file(root / "locked" / "secret.txt");
  index();
  ASSERT_TRUE(store->find_file((root / "locked" / "secret.txt").string()));
  fs::permissions(root / "locked", fs::perms::none);
  const auto r = index();
  EXPECT_EQ(r.removed, 0u);  // we cannot tell whether the file is gone, so it stays
  EXPECT_TRUE(store->find_file((root / "locked" / "secret.txt").string()));
}

TEST_F(IndexerTest, ProgressCallbackIsInvoked) {
  for (int i = 0; i < 600; ++i) write_file(root / "bulk" / ("f" + std::to_string(i) + ".txt"));
  int calls = 0;
  uint64_t last = 0;
  Indexer(*store).index(root, {}, [&](const IndexReport& r) {
    ++calls;
    last = r.files_seen;
  });
  EXPECT_GE(calls, 3);
  EXPECT_EQ(last, 611u);
}

TEST_F(IndexerTest, LargeBatchesCommitAndPersist) {
  for (int i = 0; i < 250; ++i) write_file(root / "bulk" / ("f" + std::to_string(i) + ".txt"));
  IndexOptions o;
  o.batch_size = 50;  // forces several intermediate commits
  const auto r = index(o);
  EXPECT_EQ(r.added, 261u);
  EXPECT_EQ(store->file_count(), 261u);
  EXPECT_FALSE(store->in_transaction());
}

TEST_F(IndexerTest, CancellationKeepsWorkAndSkipsDeletion) {
  index();
  fs::remove(root / "photos" / "vacation-2023.jpg");
  const auto r = Indexer(*store).index(root, {}, {}, [] { return true; });
  EXPECT_TRUE(r.cancelled);
  EXPECT_EQ(r.removed, 0u);  // an interrupted scan must not delete anything
  EXPECT_TRUE(store->find_file((root / "photos" / "vacation-2023.jpg").string()));
}

TEST_F(IndexerTest, RootsAreRecordedWithTheirOptions) {
  IndexOptions o;
  o.include_hidden = true;
  o.excludes = {"*.tmp"};
  index(o);
  const auto roots = store->list_roots();
  ASSERT_EQ(roots.size(), 1u);
  EXPECT_EQ(roots[0].path, root.string());
  EXPECT_TRUE(roots[0].include_hidden);
  EXPECT_FALSE(roots[0].follow_symlinks);
  EXPECT_EQ(roots[0].excludes, std::vector<std::string>{"*.tmp"});
}

TEST_F(IndexerTest, MissingOrNonDirectoryRootsThrow) {
  EXPECT_THROW(Indexer(*store).index(tmp.path() / "missing", {}), Error);
  EXPECT_THROW(Indexer(*store).index(root / "code" / "main.cpp", {}), Error);
  EXPECT_EQ(store->file_count(), 0u);
}

TEST_F(IndexerTest, OverlappingRootsDoNotDuplicateFiles) {
  Indexer(*store).index(root, {});
  Indexer(*store).index(root / "documents", {});
  EXPECT_EQ(store->file_count(), 11u);
}

TEST_F(IndexerTest, IndexingNeverModifiesFiles) {
  struct Snapshot { off_t size; time_t mtime; };
  std::map<std::string, Snapshot> before;
  for (auto& e : fs::recursive_directory_iterator(root)) {
    if (!e.is_regular_file()) continue;
    struct stat st{};
    ASSERT_EQ(stat(e.path().c_str(), &st), 0);
    before[e.path().string()] = {st.st_size, st.st_mtime};
  }
  IndexOptions o;
  o.include_hidden = true;
  index(o);
  index(o);
  for (auto& [path, snap] : before) {
    struct stat st{};
    ASSERT_EQ(stat(path.c_str(), &st), 0) << path;
    EXPECT_EQ(st.st_size, snap.size) << path;
    EXPECT_EQ(st.st_mtime, snap.mtime) << path;
  }
}
