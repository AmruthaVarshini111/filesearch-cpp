#include <algorithm>
#include <gtest/gtest.h>

#include "test_helpers.hpp"

using namespace filesearch;
using namespace filesearch::testing;

namespace {
class CliTest : public ::testing::Test {
 protected:
  void SetUp() override {
    root = copy_sample_data(tmp.path());
    db = tmp.path() / "state" / "index.db";
  }
  CliResult index() { return run_cli({"index", root.string()}, db); }
  TempDir tmp;
  fs::path root, db;
};
}  // namespace

TEST_F(CliTest, HelpAndVersion) {
  auto r = run_cli_raw({"help"});
  EXPECT_EQ(r.code, 0);
  for (const char* cmd : {"index", "search", "stats", "update", "remove", "rebuild"})
    EXPECT_TRUE(contains(r.out, cmd)) << cmd;
  r = run_cli_raw({"help", "search"});
  EXPECT_EQ(r.code, 0);
  EXPECT_TRUE(contains(r.out, "--min-size"));
  EXPECT_EQ(cli::run({}, std::cout, std::cerr), 0);
  r = run_cli_raw({"--version"});
  EXPECT_TRUE(contains(r.out, "filesearch "));
  EXPECT_FALSE(fs::exists(db));  // help never creates a database
}

TEST_F(CliTest, UsageErrorsExitWithTwo) {
  EXPECT_EQ(run_cli({"frobnicate"}, db).code, 2);
  EXPECT_EQ(run_cli({"index"}, db).code, 2);
  EXPECT_EQ(run_cli({"search"}, db).code, 2);
  EXPECT_EQ(run_cli({"search", "x", "--bogus"}, db).code, 2);
  EXPECT_EQ(run_cli({"search", "x", "--limit"}, db).code, 2);
  EXPECT_EQ(run_cli({"remove"}, db).code, 2);
  auto r = run_cli({"search", "x", "--limit", "abc"}, db);
  EXPECT_EQ(r.code, 2);
  EXPECT_TRUE(contains(r.err, "--limit"));
}

TEST_F(CliTest, InvalidFilterValuesGiveHelpfulMessages) {
  ASSERT_EQ(index().code, 0);
  auto r = run_cli({"search", "x", "--min-size", "lots"}, db);
  EXPECT_EQ(r.code, 2);
  EXPECT_TRUE(contains(r.err, "invalid size"));
  r = run_cli({"search", "x", "--modified-after", "2024-02-31"}, db);
  EXPECT_EQ(r.code, 2);
  EXPECT_TRUE(contains(r.err, "invalid date"));
  r = run_cli({"search", "x", "--min-size", "2MB", "--max-size", "1MB"}, db);
  EXPECT_EQ(r.code, 2);
  r = run_cli({"search", "x", "--sort", "colour"}, db);
  EXPECT_EQ(r.code, 2);
}

TEST_F(CliTest, SearchWithoutIndexExplainsWhatToDo) {
  const auto r = run_cli({"search", "x"}, db);
  EXPECT_EQ(r.code, 1);
  EXPECT_TRUE(contains(r.err, "filesearch index"));
  EXPECT_FALSE(fs::exists(db));
  EXPECT_EQ(run_cli({"stats"}, db).code, 1);
  EXPECT_EQ(run_cli({"update"}, db).code, 1);
}

TEST_F(CliTest, IndexThenSearchEndToEnd) {
  const auto idx = index();
  ASSERT_EQ(idx.code, 0) << idx.err;
  EXPECT_TRUE(contains(idx.out, "11 scanned"));
  EXPECT_TRUE(fs::exists(db));

  auto r = run_cli({"search", "machine learning", "--type", "pdf"}, db);
  ASSERT_EQ(r.code, 0) << r.err;
  EXPECT_TRUE(contains(r.out, "machine-learning-intro.pdf"));
  EXPECT_TRUE(contains(r.out, (root / "documents" / "machine-learning-intro.pdf").string()));
  EXPECT_FALSE(contains(r.out, "notes.txt"));
  EXPECT_TRUE(contains(r.out, "1 result"));

  r = run_cli({"search", "resume", "--directory", (root / "documents").string()}, db);
  EXPECT_TRUE(contains(r.out, "resume_2024.txt"));
  r = run_cli({"search", "resume", "--directory", (root / "code").string()}, db);
  EXPECT_TRUE(contains(r.out, "No files found"));
}

TEST_F(CliTest, TypeAcceptsDotsCommasAndRepeats) {
  ASSERT_EQ(index().code, 0);
  auto r = run_cli({"search", "learning", "-t", ".PDF,md", "-p"}, db);
  EXPECT_TRUE(contains(r.out, "machine-learning-intro.pdf"));
  EXPECT_TRUE(contains(r.out, "deep_learning_survey.md"));
  EXPECT_FALSE(contains(r.out, "notes.txt"));
  r = run_cli({"search", "learning", "-t", "pdf", "-t", "txt", "-p"}, db);
  EXPECT_TRUE(contains(r.out, "notes.txt"));
}

TEST_F(CliTest, QueryWordsNeedNoQuotesAndDashQueriesWork) {
  ASSERT_EQ(index().code, 0);
  EXPECT_TRUE(contains(run_cli({"search", "machine", "learning", "-p"}, db).out, "intro.pdf"));
  EXPECT_EQ(run_cli_raw({"search", "--db", db.string(), "--", "-resume"}).code, 0);
}

TEST_F(CliTest, LimitAndSizeFilterFromTheCommandLine) {
  write_bytes(root / "big" / "huge-report.bin", 2500000);
  ASSERT_EQ(index().code, 0);
  auto r = run_cli({"search", "report", "--min-size", "1MB", "-p"}, db);
  EXPECT_EQ(r.out, (root / "big" / "huge-report.bin").string() + "\n");
  r = run_cli({"search", "report", "--limit", "2", "-p"}, db);
  EXPECT_EQ(std::count(r.out.begin(), r.out.end(), '\n'), 2);
  r = run_cli({"search", "report", "--limit", "1"}, db);
  EXPECT_TRUE(contains(r.out, "use --limit"));
}

TEST_F(CliTest, DateFilterFromTheCommandLine) {
  set_mtime(root / "documents" / "resume_2024.txt", 1577836800);  // 2020-01-01
  ASSERT_EQ(index().code, 0);
  auto r = run_cli({"search", "resume", "--modified-before", "2021-01-01", "-p"}, db);
  EXPECT_TRUE(contains(r.out, "resume_2024.txt"));
  r = run_cli({"search", "resume", "--modified-after", "2021-01-01", "-p"}, db);
  EXPECT_FALSE(contains(r.out, "resume_2024.txt"));
  r = run_cli({"search", "resume", "--modified-after", "1d", "-p"}, db);
  EXPECT_FALSE(contains(r.out, "resume_2024.txt"));
}

TEST_F(CliTest, JsonOutputIsWellFormedEnough) {
  ASSERT_EQ(index().code, 0);
  const auto r = run_cli({"search", "resume", "--json"}, db);
  EXPECT_EQ(r.out.front(), '{');
  EXPECT_TRUE(contains(r.out, "\"results\":[{\"rank\":1"));
  EXPECT_TRUE(contains(r.out, "\"name\":\"resume_2024.txt\""));
  EXPECT_TRUE(contains(r.out, "\"elapsed_ms\""));
}

TEST_F(CliTest, StatsShowsCountsAndRoots) {
  ASSERT_EQ(index().code, 0);
  const auto r = run_cli({"stats", "--verify"}, db);
  ASSERT_EQ(r.code, 0) << r.err;
  EXPECT_TRUE(contains(r.out, "Files         11"));
  EXPECT_TRUE(contains(r.out, root.string()));
  EXPECT_TRUE(contains(r.out, "Integrity check: ok"));
}

TEST_F(CliTest, UpdateDetectsChanges) {
  ASSERT_EQ(index().code, 0);
  write_file(root / "documents" / "brand-new.txt");
  fs::remove(root / "code" / "README.md");
  const auto r = run_cli({"update"}, db);
  ASSERT_EQ(r.code, 0) << r.err;
  EXPECT_TRUE(contains(r.out, "1 new"));
  EXPECT_TRUE(contains(r.out, "1 removed"));
  EXPECT_TRUE(contains(run_cli({"search", "brand", "-p"}, db).out, "brand-new.txt"));
}

TEST_F(CliTest, UpdateReusesStoredOptions) {
  ASSERT_EQ(run_cli({"index", root.string(), "--hidden", "--exclude", "*.csv"}, db).code, 0);
  EXPECT_TRUE(contains(run_cli({"search", "secret", "-p"}, db).out, "secret.txt"));
  EXPECT_FALSE(contains(run_cli({"search", "budget", "-p"}, db).out, "budget"));
  write_file(root / ".hidden" / "second.txt");
  write_file(root / "documents" / "extra.csv");
  ASSERT_EQ(run_cli({"update"}, db).code, 0);
  EXPECT_TRUE(contains(run_cli({"search", "second", "-p"}, db).out, "second.txt"));
  EXPECT_FALSE(contains(run_cli({"search", "extra", "-p"}, db).out, "extra.csv"));
}

TEST_F(CliTest, UpdateWarnsAboutMissingRootAndKeepsEntries) {
  ASSERT_EQ(index().code, 0);
  fs::rename(root, tmp.path() / "moved");
  const auto r = run_cli({"update"}, db);
  EXPECT_EQ(r.code, 1);
  EXPECT_TRUE(contains(r.err, "not available"));
  EXPECT_TRUE(contains(run_cli({"search", "resume", "-p"}, db).out, "resume_2024.txt"));
}

TEST_F(CliTest, RemoveDropsDirectoryButNotFiles) {
  ASSERT_EQ(index().code, 0);
  const auto r = run_cli({"remove", (root / "documents").string()}, db);
  ASSERT_EQ(r.code, 0) << r.err;
  EXPECT_TRUE(contains(r.out, "not touched"));
  EXPECT_TRUE(contains(run_cli({"search", "resume"}, db).out, "No files found"));
  EXPECT_TRUE(fs::exists(root / "documents" / "resume_2024.txt"));
  EXPECT_EQ(run_cli({"remove", (root / "documents").string()}, db).code, 1);  // already gone
}

TEST_F(CliTest, RebuildRecreatesTheIndex) {
  ASSERT_EQ(index().code, 0);
  write_file(root / "documents" / "after-index.txt");
  const auto r = run_cli({"rebuild"}, db);
  ASSERT_EQ(r.code, 0) << r.err;
  EXPECT_TRUE(contains(r.out, "12 scanned"));
  EXPECT_TRUE(contains(r.out, "12 new"));
  EXPECT_TRUE(contains(run_cli({"search", "after", "-p"}, db).out, "after-index.txt"));
}

TEST_F(CliTest, CorruptDatabaseIsHandledGracefully) {
  fs::create_directories(db.parent_path());
  write_file(db, std::string(8192, 'q'));
  auto r = run_cli({"search", "x"}, db);
  EXPECT_EQ(r.code, 1);
  EXPECT_TRUE(contains(r.err, "corrupted"));
  EXPECT_TRUE(contains(r.err, "filesearch rebuild"));

  r = run_cli({"rebuild"}, db);
  EXPECT_EQ(r.code, 1);
  EXPECT_TRUE(contains(r.err, ".corrupt-"));
  EXPECT_FALSE(fs::exists(db));  // moved aside, not deleted
  int backups = 0;
  for (auto& e : fs::directory_iterator(db.parent_path()))
    if (contains(e.path().filename().string(), ".corrupt-")) ++backups;
  EXPECT_EQ(backups, 1);

  ASSERT_EQ(index().code, 0);  // a fresh index works afterwards
  EXPECT_TRUE(contains(run_cli({"search", "resume", "-p"}, db).out, "resume_2024.txt"));
}

TEST_F(CliTest, IndexingMissingDirectoryFailsCleanly) {
  const auto r = run_cli({"index", (tmp.path() / "nope").string()}, db);
  EXPECT_EQ(r.code, 1);
  EXPECT_TRUE(contains(r.err, "no such directory"));
}

TEST_F(CliTest, OpenAndRevealValidateTheResultNumber) {
  ASSERT_EQ(index().code, 0);
  EXPECT_EQ(run_cli({"search", "resume", "--open", "0"}, db).code, 2);
  const auto r = run_cli({"search", "resume", "--open", "9"}, db);
  EXPECT_EQ(r.code, 1);
  EXPECT_TRUE(contains(r.err, "no result #9"));
  EXPECT_EQ(run_cli({"search", "resume", "--open", "1", "--reveal", "1"}, db).code, 2);
}

TEST_F(CliTest, QuietIndexingPrintsNothing) {
  const auto r = run_cli({"index", root.string(), "--quiet"}, db);
  EXPECT_EQ(r.code, 0);
  EXPECT_TRUE(r.out.empty());
}

TEST_F(CliTest, OutputIsPlainTextWhenNotATerminal) {
  ASSERT_EQ(index().code, 0);
  const auto r = run_cli({"search", "resume"}, db);
  EXPECT_FALSE(contains(r.out, "\033["));
}
