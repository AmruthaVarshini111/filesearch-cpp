#include <filesystem>

#include "cli/commands.hpp"
#include "utils/error.hpp"
#include "utils/format_utils.hpp"

namespace filesearch::cli {
namespace fs = std::filesystem;

int cmd_stats(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  const auto p = parse_args(args, common_options({{"verify", 0, false}}));
  if (p.has("help")) {
    out << command_help("stats");
    return 0;
  }
  if (!p.positional().empty()) throw UsageError("stats takes no arguments");
  Context ctx = make_context(p, out, err, false);
  const auto& t = ctx.term;

  IndexStore store(ctx.db_path, OpenMode::MustExist);
  const StoreStats s = store.stats();
  const auto roots = store.list_roots();

  out << t.bold("FileSearch index") << "\n";
  out << "  Database      " << ctx.db_path.string() << " (" << fmt::format_size(s.db_bytes) << ")\n";
  out << "  Files         " << fmt::format_number(s.files) << "\n";
  out << "  Total size    " << fmt::format_size(s.total_size) << "\n";
  out << "  Unique terms  " << fmt::format_number(s.terms) << "\n";
  out << "  Postings      " << fmt::format_number(s.postings) << "\n";
  if (!s.top_extensions.empty()) {
    out << "  Top types     ";
    for (size_t i = 0; i < s.top_extensions.size(); ++i) {
      if (i) out << ", ";
      out << (s.top_extensions[i].first.empty() ? "(none)" : s.top_extensions[i].first) << " "
          << fmt::format_number(s.top_extensions[i].second);
    }
    out << "\n";
  }
  out << "\n" << t.bold("Indexed directories") << "\n";
  if (roots.empty()) out << "  (none)\n";
  for (const auto& r : roots) {
    std::string opts;
    if (r.include_hidden) opts += " hidden";
    if (r.follow_symlinks) opts += " follow-symlinks";
    for (const auto& e : r.excludes) opts += " exclude=" + e;
    out << "  " << r.path << "\n"
        << t.dim("      last indexed " + fmt::format_time(r.last_indexed) +
                 (opts.empty() ? "" : ", options:" + opts))
        << "\n";
  }
  if (p.has("verify")) {
    const std::string result = store.integrity_check();
    out << "\nIntegrity check: " << (result == "ok" ? t.green("ok") : t.red(result)) << "\n";
    if (result != "ok") return 1;
  }
  return 0;
}

}  // namespace filesearch::cli
