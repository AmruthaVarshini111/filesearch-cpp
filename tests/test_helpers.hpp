#pragma once
// Shared helpers. Every test works inside a private temporary directory; no
// test ever reads or writes your real files or your real FileSearch database.
#include <gtest/gtest.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>

#include "cli/commands.hpp"

namespace filesearch::testing {
namespace fs = std::filesystem;

class TempDir {
 public:
  TempDir() {
    std::string pattern = (fs::temp_directory_path() / "filesearch-test-XXXXXX").string();
    std::vector<char> buf(pattern.begin(), pattern.end());
    buf.push_back('\0');
    if (!mkdtemp(buf.data())) throw std::runtime_error("mkdtemp failed");
    path_ = buf.data();
  }
  ~TempDir() {
    std::error_code ec;
    // Safety net: only ever delete directories we created ourselves.
    if (path_.filename().string().rfind("filesearch-test-", 0) == 0) {
      // Restore permissions so cleanup works after permission tests.
      for (auto& e : fs::recursive_directory_iterator(
               path_, fs::directory_options::skip_permission_denied, ec)) {
        if (e.is_directory(ec)) fs::permissions(e.path(), fs::perms::owner_all, fs::perm_options::add, ec);
      }
      fs::remove_all(path_, ec);
    }
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

inline void write_file(const fs::path& p, const std::string& content = "x") {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << content;
}

inline void write_bytes(const fs::path& p, size_t n) { write_file(p, std::string(n, 'a')); }

inline void set_mtime(const fs::path& p, int64_t epoch) {
  timeval tv[2];
  tv[0].tv_sec = tv[1].tv_sec = static_cast<time_t>(epoch);
  tv[0].tv_usec = tv[1].tv_usec = 0;
  ASSERT_EQ(utimes(p.c_str(), tv), 0);
}

/// Copies the committed sample dataset into `dest/sample` and returns that path.
inline fs::path copy_sample_data(const fs::path& dest) {
  const fs::path target = dest / "sample";
  fs::copy(FILESEARCH_SAMPLE_DATA, target, fs::copy_options::recursive);
  return target;
}

struct CliResult {
  int code = 0;
  std::string out;
  std::string err;
};

/// Runs the CLI with exactly these arguments (no --db appended).
inline CliResult run_cli_raw(const std::vector<std::string>& args) {
  std::ostringstream out, err;
  CliResult r;
  r.code = cli::run(args, out, err);
  r.out = out.str();
  r.err = err.str();
  return r;
}

inline CliResult run_cli(std::vector<std::string> args, const fs::path& db) {
  args.push_back("--db");
  args.push_back(db.string());
  std::ostringstream out, err;
  CliResult r;
  r.code = cli::run(args, out, err);
  r.out = out.str();
  r.err = err.str();
  return r;
}

inline bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace filesearch::testing
