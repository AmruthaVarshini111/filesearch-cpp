#include "indexer/indexer.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <set>
#include <unordered_set>

#include "search/term_extractor.hpp"
#include "utils/error.hpp"
#include "utils/path_utils.hpp"
#include "utils/string_utils.hpp"

namespace filesearch {
namespace fs = std::filesystem;
namespace {

constexpr size_t kMaxErrorSamples = 10;
constexpr uint64_t kProgressEvery = 256;

int64_t now_seconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

struct StatInfo {
  bool ok = false;
  bool is_reg = false;
  bool is_dir = false;
  uint64_t size = 0;
  int64_t mtime = 0;
};

StatInfo to_info(const struct stat& st) {
  StatInfo i;
  i.ok = true;
  i.is_reg = S_ISREG(st.st_mode);
  i.is_dir = S_ISDIR(st.st_mode);
  i.size = static_cast<uint64_t>(st.st_size);
  i.mtime = static_cast<int64_t>(st.st_mtime);
  return i;
}

StatInfo stat_follow(const std::string& p) {
  struct stat st {};
  return ::stat(p.c_str(), &st) == 0 ? to_info(st) : StatInfo{};
}

StatInfo stat_nofollow(const std::string& p) {
  struct stat st {};
  return ::lstat(p.c_str(), &st) == 0 ? to_info(st) : StatInfo{};
}

std::string lower_extension(const std::string& name) {
  const std::string ext = fs::path(name).extension().string();  // ".gz", or "" for ".bashrc"
  if (ext.size() <= 1) return {};
  return strutil::to_lower(ext.substr(1));
}

class Walker {
 public:
  Walker(IndexStore& store, const IndexOptions& opt, const ProgressFn& progress,
         const CancelFn& cancelled, IndexReport& report)
      : store_(store), opt_(opt), progress_(progress), cancelled_(cancelled), report_(report) {}

  void run(const std::string& root) {
    std::vector<std::string> pending{root};
    if (opt_.follow_symlinks) {
      std::error_code ec;
      visited_.insert(fs::canonical(root, ec).string());
    }
    store_.begin();
    try {
      while (!pending.empty() && !cancel_requested()) {
        std::string dir = std::move(pending.back());
        pending.pop_back();
        scan_directory(dir, pending);
      }
      if (!report_.cancelled) remove_missing(root);
      store_.prune_terms();
      store_.commit();
    } catch (...) {
      store_.rollback();
      throw;
    }
  }

 private:
  bool cancel_requested() {
    if (!report_.cancelled && cancelled_ && cancelled_()) report_.cancelled = true;
    return report_.cancelled;
  }

  void note_error(const std::string& message) {
    ++report_.errors;
    if (report_.error_samples.size() < kMaxErrorSamples) report_.error_samples.push_back(message);
  }

  void mark_unreadable(const std::string& dir, const std::string& why) {
    ++report_.unreadable_dirs;
    unreadable_.insert(dir);
    note_error("cannot read directory " + dir + ": " + why);
  }

  bool excluded(const std::string& name) const {
    for (const auto& pat : opt_.excludes)
      if (strutil::glob_match(pat, name)) return true;
    return false;
  }

  void scan_directory(const std::string& dir, std::vector<std::string>& pending) {
    std::error_code ec;
    fs::directory_iterator it(dir, ec);  // NOT skip_permission_denied: errors must be visible
    if (ec) {
      mark_unreadable(dir, ec.message());
      return;
    }
    ++report_.dirs_scanned;
    const fs::directory_iterator end;
    for (; it != end; it.increment(ec)) {
      if (ec) break;
      const std::string name = it->path().filename().string();
      // Ignore macOS Finder metadata files.
      if (name == ".DS_Store") {
        continue;
      }
      if (!opt_.include_hidden && pathutil::is_hidden_name(name)) {
        ++report_.skipped_hidden;
        continue;
      }
      if (excluded(name)) {
        ++report_.skipped_excluded;
        continue;
      }
      std::error_code sec;
      const fs::file_status ls = it->symlink_status(sec);
      if (sec) {
        note_error("cannot stat " + it->path().string() + ": " + sec.message());
        continue;
      }
      const std::string full = pathutil::join(dir, name);
      if (fs::is_directory(ls)) {
        if (!opt_.follow_symlinks || enter_once(full)) pending.push_back(full);
      } else if (fs::is_regular_file(ls)) {
        process_file(full, name, stat_nofollow(full));
      } else if (fs::is_symlink(ls)) {
        handle_symlink(full, name, pending);
      }
      // sockets, FIFOs and devices are ignored
      if (cancel_requested()) return;
    }
    if (ec) {
      // The listing stopped half-way: keep everything below this directory.
      mark_unreadable(dir, ec.message());
    }
  }

  // With follow_symlinks, every directory is entered at most once (by canonical path).
  bool enter_once(const std::string& dir) {
    std::error_code ec;
    const fs::path canon = fs::canonical(dir, ec);
    if (ec) return false;
    return visited_.insert(canon.string()).second;
  }

  void handle_symlink(const std::string& full, const std::string& name,
                      std::vector<std::string>& pending) {
    const StatInfo target = stat_follow(full);
    if (!target.ok) {  // dangling link
      ++report_.symlinks_skipped;
      return;
    }
    if (target.is_dir) {
      if (opt_.follow_symlinks && enter_once(full))
        pending.push_back(full);
      else
        ++report_.symlinks_skipped;
    } else if (target.is_reg) {
      process_file(full, name, target);
    }
  }

  void process_file(const std::string& full, const std::string& name, const StatInfo& info) {
    if (!info.ok) {
      note_error("cannot stat " + full);
      return;
    }
    ++report_.files_seen;
    FileMeta meta;
    meta.path = full;
    meta.name = name;
    meta.ext = lower_extension(name);
    meta.size = info.size;
    meta.mtime = info.mtime;

    auto existing = store_.find_file(full);
    if (existing && existing->size == meta.size && existing->mtime == meta.mtime) {
      ++report_.unchanged;
      seen_.insert(existing->id);
    } else {
      const DocumentTerms doc = extract_terms(name, pathutil::parent_of(full));
      int64_t id;
      if (existing) {
        id = existing->id;
        store_.update_file(id, meta, doc.name_token_count);
        ++report_.updated;
      } else {
        id = store_.insert_file(meta, doc.name_token_count);
        ++report_.added;
      }
      store_.set_postings(id, doc.terms);
      seen_.insert(id);
      if (++pending_writes_ >= opt_.batch_size) {
        store_.commit();
        store_.begin();
        pending_writes_ = 0;
      }
    }
    if (progress_ && report_.files_seen % kProgressEvery == 0) progress_(report_);
  }

  bool under_unreadable(const std::string& path) const {
    if (unreadable_.empty()) return false;
    for (std::string p = pathutil::parent_of(path);; p = pathutil::parent_of(p)) {
      if (unreadable_.count(p)) return true;
      if (p == "/" || p == ".") return false;
    }
  }

  void remove_missing(const std::string& root) {
    std::vector<int64_t> gone;
    store_.for_each_file_under(root, [&](int64_t id, const std::string& path) {
      if (!seen_.count(id) && !under_unreadable(path)) gone.push_back(id);
    });
    for (int64_t id : gone) store_.delete_file(id);
    report_.removed = gone.size();
  }

  IndexStore& store_;
  const IndexOptions& opt_;
  const ProgressFn& progress_;
  const CancelFn& cancelled_;
  IndexReport& report_;
  std::unordered_set<int64_t> seen_;
  std::unordered_set<std::string> unreadable_;
  std::set<std::string> visited_;
  size_t pending_writes_ = 0;
};

}  // namespace

IndexReport Indexer::index(const fs::path& root_in, const IndexOptions& options,
                           const ProgressFn& progress, const CancelFn& cancelled) {
  const auto t0 = std::chrono::steady_clock::now();
  const std::string root = pathutil::absolute_normal(root_in).string();

  std::error_code ec;
  if (!fs::exists(root, ec)) throw Error("no such directory: " + root);
  if (!fs::is_directory(root, ec)) throw Error("not a directory: " + root);
  {
    fs::directory_iterator probe(root, ec);
    if (ec) throw Error("cannot read directory " + root + ": " + ec.message());
  }

  IndexReport report;
  Walker walker(store_, options, progress, cancelled, report);
  walker.run(root);

  RootInfo info;
  info.path = root;
  info.include_hidden = options.include_hidden;
  info.follow_symlinks = options.follow_symlinks;
  info.excludes = options.excludes;
  info.last_indexed = now_seconds();
  store_.upsert_root(info);
  store_.checkpoint();

  report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (progress) progress(report);
  return report;
}

}  // namespace filesearch
