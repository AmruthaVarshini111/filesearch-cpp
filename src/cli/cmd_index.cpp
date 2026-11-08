#include <csignal>
#include <ctime>
#include <filesystem>

#include "cli/commands.hpp"
#include "utils/error.hpp"
#include "utils/format_utils.hpp"
#include "utils/path_utils.hpp"
#include "utils/string_utils.hpp"

namespace filesearch::cli {
namespace fs = std::filesystem;
namespace {

volatile std::sig_atomic_t g_interrupted = 0;
void on_sigint(int) { g_interrupted = 1; }

class SigintGuard {
 public:
  SigintGuard() {
    g_interrupted = 0;
    old_ = std::signal(SIGINT, on_sigint);
  }
  ~SigintGuard() { std::signal(SIGINT, old_); }

 private:
  void (*old_)(int);
};

std::string plural(uint64_t n, const char* one, const char* many) {
  return fmt::format_number(n) + " " + (n == 1 ? one : many);
}

void print_summary(Context& ctx, const fs::path& root, const IndexReport& r) {
  const auto& t = ctx.term;
  ctx.out << t.bold(r.cancelled ? "Interrupted " : "Indexed ") << root.string() << "\n";
  ctx.out << "  Files     " << fmt::format_number(r.files_seen) << " scanned: "
          << fmt::format_number(r.added) << " new, " << fmt::format_number(r.updated)
          << " updated, " << fmt::format_number(r.unchanged) << " unchanged, "
          << fmt::format_number(r.removed) << " removed\n";
  std::vector<std::string> skipped;
  if (r.skipped_hidden) skipped.push_back(fmt::format_number(r.skipped_hidden) + " hidden");
  if (r.skipped_excluded) skipped.push_back(fmt::format_number(r.skipped_excluded) + " excluded");
  if (r.symlinks_skipped) skipped.push_back(plural(r.symlinks_skipped, "symlink", "symlinks"));
  if (!skipped.empty()) ctx.out << "  Skipped   " << strutil::join(skipped, ", ") << "\n";
  char buf[64];
  const double rate = r.seconds > 0 ? static_cast<double>(r.files_seen) / r.seconds : 0;
  std::snprintf(buf, sizeof buf, "%.2f s (%s files/s)", r.seconds,
                fmt::format_number(static_cast<uint64_t>(rate)).c_str());
  ctx.out << "  Time      " << buf << "\n";
}

void print_warnings(Context& ctx, const IndexReport& r) {
  if (r.errors == 0) return;
  ctx.err << ctx.err_term.yellow("Warning: ") << plural(r.errors, "problem", "problems")
          << " while scanning (" << plural(r.unreadable_dirs, "directory", "directories")
          << " could not be read). Entries below unreadable directories were left untouched.\n";
  bool permission = false;
  for (const auto& s : r.error_samples) {
    ctx.err << "  " << s << "\n";
    if (s.find("ermission") != std::string::npos || s.find("not permitted") != std::string::npos)
      permission = true;
  }
  if (r.errors > r.error_samples.size())
    ctx.err << "  ... and " << (r.errors - r.error_samples.size()) << " more\n";
#ifdef __APPLE__
  if (permission)
    ctx.err << "  Hint: macOS protects folders such as Desktop, Documents and Library. Grant your\n"
               "  terminal 'Full Disk Access' (System Settings > Privacy & Security) to index them.\n";
#else
  (void)permission;
#endif
}

IndexOptions options_from(const ParsedArgs& p) {
  IndexOptions o;
  o.include_hidden = p.has("hidden");
  o.follow_symlinks = p.has("follow-symlinks");
  o.excludes = p.get_all("exclude");
  return o;
}

}  // namespace

IndexReport index_with_feedback(Context& ctx, IndexStore& store, const fs::path& root,
                                const IndexOptions& options) {
  SigintGuard guard;
  ProgressLine progress(ctx.err, ctx.show_progress);
  const std::string shown = pathutil::absolute_normal(root).string();
  auto on_progress = [&](const IndexReport& r) {
    progress.update("  Indexing " + shown + ": " + fmt::format_number(r.files_seen) + " files (" +
                    fmt::format_number(r.added) + " new, " + fmt::format_number(r.updated) +
                    " changed)");
  };
  Indexer indexer(store);
  IndexReport report =
      indexer.index(root, options, on_progress, [] { return g_interrupted != 0; });
  progress.finish();
  if (!ctx.quiet) print_summary(ctx, shown, report);
  print_warnings(ctx, report);
  if (report.cancelled)
    ctx.err << "Interrupted: the work done so far was saved. Run the command again to continue.\n";
  return report;
}

int cmd_index(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  const auto p = parse_args(args, common_options({{"hidden", 0, false},
                                                  {"follow-symlinks", 0, false},
                                                  {"exclude", 0, true},
                                                  {"quiet", 'q', false},
                                                  {"no-progress", 0, false}}));
  if (p.has("help")) {
    out << command_help("index");
    return 0;
  }
  if (p.positional().empty())
    throw UsageError("index needs a directory, e.g.: filesearch index ~/Documents");
  Context ctx = make_context(p, out, err, true);
  const IndexOptions opts = options_from(p);

  IndexStore store(ctx.db_path, OpenMode::CreateIfMissing);
  int rc = 0;
  for (const auto& dir : p.positional()) {
    try {
      const IndexReport r = index_with_feedback(ctx, store, fs::path(pathutil::expand_user(dir)), opts);
      if (r.cancelled) return 130;
    } catch (const DatabaseError&) {
      throw;
    } catch (const Error& e) {
      err << "filesearch: " << e.what() << "\n";
      rc = 1;
    }
  }
  return rc;
}

int cmd_update(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  const auto p = parse_args(
      args, common_options({{"quiet", 'q', false}, {"no-progress", 0, false}}));
  if (p.has("help")) {
    out << command_help("update");
    return 0;
  }
  if (!p.positional().empty()) throw UsageError("update takes no arguments");
  Context ctx = make_context(p, out, err, true);
  IndexStore store(ctx.db_path, OpenMode::MustExist);
  const auto roots = store.list_roots();
  if (roots.empty()) {
    out << "Nothing to update: no directories are indexed yet.\n";
    return 0;
  }
  int rc = 0;
  for (const auto& root : roots) {
    std::error_code ec;
    if (!fs::is_directory(root.path, ec)) {
      err << ctx.err_term.yellow("Warning: ") << root.path
          << " is not available (moved, deleted or unmounted); its entries were kept. "
             "Use 'filesearch remove' to drop them.\n";
      rc = 1;
      continue;
    }
    IndexOptions o;
    o.include_hidden = root.include_hidden;
    o.follow_symlinks = root.follow_symlinks;
    o.excludes = root.excludes;
    try {
      if (index_with_feedback(ctx, store, root.path, o).cancelled) return 130;
    } catch (const DatabaseError&) {
      throw;
    } catch (const Error& e) {
      err << "filesearch: " << e.what() << "\n";
      rc = 1;
    }
  }
  return rc;
}

int cmd_remove(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  const auto p = parse_args(args, common_options({}));
  if (p.has("help")) {
    out << command_help("remove");
    return 0;
  }
  if (p.positional().size() != 1)
    throw UsageError("remove needs exactly one directory, e.g.: filesearch remove ~/Downloads");
  Context ctx = make_context(p, out, err, false);
  const std::string dir = pathutil::absolute_normal(p.positional()[0]).string();
  IndexStore store(ctx.db_path, OpenMode::MustExist);
  const RemoveResult r = store.remove_directory(dir);
  store.checkpoint();
  if (r.files_removed == 0 && r.roots_removed == 0) {
    err << "filesearch: nothing in the index is located at " << dir << "\n";
    return 1;
  }
  out << "Removed " << plural(r.files_removed, "file", "files") << " from the index below " << dir
      << ". Your files on disk were not touched.\n";
  return 0;
}

namespace {

std::string timestamp_now() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tm);
  return buf;
}

int rebuild_inner(Context& ctx) {
  IndexStore store(ctx.db_path, OpenMode::MustExist);
  const auto roots = store.list_roots();
  if (roots.empty()) {
    ctx.out << "Nothing to rebuild: no directories are indexed yet.\n";
    return 0;
  }
  store.clear_index();
  store.vacuum();
  int rc = 0;
  for (const auto& root : roots) {
    IndexOptions o;
    o.include_hidden = root.include_hidden;
    o.follow_symlinks = root.follow_symlinks;
    o.excludes = root.excludes;
    try {
      if (index_with_feedback(ctx, store, root.path, o).cancelled) return 130;
    } catch (const DatabaseError&) {
      throw;
    } catch (const Error& e) {
      ctx.err << "filesearch: " << e.what() << "\n";
      rc = 1;
    }
  }
  return rc;
}

}  // namespace

int cmd_rebuild(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  const auto p = parse_args(
      args, common_options({{"quiet", 'q', false}, {"no-progress", 0, false}}));
  if (p.has("help")) {
    out << command_help("rebuild");
    return 0;
  }
  if (!p.positional().empty()) throw UsageError("rebuild takes no arguments");
  Context ctx = make_context(p, out, err, true);
  try {
    return rebuild_inner(ctx);
  } catch (const CorruptDatabaseError& e) {
    // Move the broken database (and its WAL files) aside; never delete it.
    const std::string stamp = ".corrupt-" + timestamp_now();
    std::error_code ec;
    for (const char* suffix : {"", "-wal", "-shm"}) {
      const fs::path src = ctx.db_path.string() + suffix;
      if (fs::exists(src, ec)) fs::rename(src, fs::path(src.string() + stamp), ec);
    }
    err << "filesearch: the index database was unreadable (" << e.what() << ").\n"
        << "It was moved to " << ctx.db_path.string() << stamp << ".\n"
        << "Create a new index with: filesearch index <directory>\n";
    return 1;
  }
}

}  // namespace filesearch::cli
