#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "cli/commands.hpp"
#include "cli/file_actions.hpp"
#include "search/searcher.hpp"
#include "utils/error.hpp"
#include "utils/format_utils.hpp"
#include "utils/path_utils.hpp"
#include "utils/string_utils.hpp"

namespace filesearch::cli {
namespace fs = std::filesystem;
namespace {

uint64_t parse_count(const std::string& text, const char* option) {
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos || text.size() > 12)
    throw UsageError(std::string("option ") + option + " needs a whole number, got '" + text + "'");
  return std::strtoull(text.c_str(), nullptr, 10);
}

uint64_t parse_size_option(const std::string& text, const char* option) {
  auto v = fmt::parse_size(text);
  if (!v)
    throw UsageError(std::string("invalid size '") + text + "' for " + option +
                     " (examples: 500, 10KB, 1MB, 2GiB)");
  return *v;
}

int64_t parse_time_option(const std::string& text, bool end_of_day, const char* option) {
  const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
  auto v = fmt::parse_time(text, end_of_day, now);
  if (!v)
    throw UsageError(std::string("invalid date '") + text + "' for " + option +
                     " (examples: 2024-01-31, today, 7d, 2w, 3m, 1y)");
  return *v;
}

SortKey parse_sort(const std::string& text) {
  const std::string s = strutil::to_lower(text);
  if (s == "relevance" || s == "score") return SortKey::Relevance;
  if (s == "size") return SortKey::Size;
  if (s == "date" || s == "modified" || s == "time") return SortKey::Date;
  if (s == "name") return SortKey::Name;
  throw UsageError("unknown sort key '" + text + "' (use relevance, size, date or name)");
}

void print_human(Context& ctx, const SearchOutcome& r, bool show_score) {
  const auto& t = ctx.term;
  if (r.results.empty()) {
    ctx.out << "No files found.\n";
    return;
  }
  const size_t width = std::to_string(r.results.size()).size();
  for (size_t i = 0; i < r.results.size(); ++i) {
    const auto& res = r.results[i];
    std::string num = std::to_string(i + 1);
    num.insert(0, width - num.size(), ' ');
    std::string meta = fmt::format_size(res.file.size) + ", " + fmt::format_time(res.file.mtime);
    if (show_score) {
      char buf[32];
      std::snprintf(buf, sizeof buf, ", score %.2f", res.score);
      meta += buf;
    }
    ctx.out << t.dim(num) << "  " << t.bold(res.file.name) << "  " << t.dim("(" + meta + ")") << "\n";
    ctx.out << std::string(width + 2, ' ') << t.cyan(res.file.path) << "\n";
  }
  char timing[64];
  std::snprintf(timing, sizeof timing, "%.1f ms", r.elapsed_ms);
  ctx.out << "\n";
  if (r.total_matches > r.results.size())
    ctx.out << t.dim("Showing " + std::to_string(r.results.size()) + " of " +
                     fmt::format_number(r.total_matches) + " matches in " + timing +
                     " (use --limit to see more)")
            << "\n";
  else
    ctx.out << t.dim(std::to_string(r.results.size()) + (r.results.size() == 1 ? " result in " : " results in ") +
                     timing)
            << "\n";
}

void print_json(Context& ctx, const SearchOptions& so, const SearchOutcome& r) {
  using strutil::json_escape;
  char buf[64];
  ctx.out << "{\"query\":\"" << json_escape(so.query) << "\",\"total_matches\":" << r.total_matches;
  std::snprintf(buf, sizeof buf, "%.3f", r.elapsed_ms);
  ctx.out << ",\"elapsed_ms\":" << buf << ",\"results\":[";
  for (size_t i = 0; i < r.results.size(); ++i) {
    const auto& f = r.results[i].file;
    if (i) ctx.out << ",";
    std::snprintf(buf, sizeof buf, "%.4f", r.results[i].score);
    ctx.out << "{\"rank\":" << (i + 1) << ",\"name\":\"" << json_escape(f.name) << "\",\"path\":\""
            << json_escape(f.path) << "\",\"size\":" << f.size << ",\"modified\":" << f.mtime
            << ",\"score\":" << buf << "}";
  }
  ctx.out << "]}\n";
}

}  // namespace

int cmd_search(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  const auto p = parse_args(args, common_options({{"type", 't', true},
                                                  {"directory", 'd', true},
                                                  {"min-size", 0, true},
                                                  {"max-size", 0, true},
                                                  {"modified-after", 0, true},
                                                  {"modified-before", 0, true},
                                                  {"name-only", 0, false},
                                                  {"exact", 0, false},
                                                  {"whole-words", 0, false},
                                                  {"any", 0, false},
                                                  {"limit", 'n', true},
                                                  {"sort", 's', true},
                                                  {"reverse", 0, false},
                                                  {"paths-only", 'p', false},
                                                  {"json", 0, false},
                                                  {"score", 0, false},
                                                  {"open", 0, true},
                                                  {"reveal", 0, true}}));
  if (p.has("help")) {
    out << command_help("search");
    return 0;
  }
  Context ctx = make_context(p, out, err, false);

  SearchOptions so;
  so.query = strutil::join(p.positional(), " ");

  for (const auto& group : p.get_all("type"))
    for (auto ext : strutil::split(group, ',')) {
      ext = strutil::to_lower(strutil::trim(ext));
      while (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
      if (!ext.empty()) so.filter.extensions.push_back(ext);
    }
  if (p.has("type") && so.filter.extensions.empty()) throw UsageError("--type needs an extension such as pdf");
  if (auto d = p.get("directory")) so.filter.directory = pathutil::absolute_normal(*d).string();
  if (auto v = p.get("min-size")) so.filter.min_size = parse_size_option(*v, "--min-size");
  if (auto v = p.get("max-size")) so.filter.max_size = parse_size_option(*v, "--max-size");
  if (so.filter.min_size && so.filter.max_size && *so.filter.min_size > *so.filter.max_size)
    throw UsageError("--min-size is larger than --max-size");
  if (auto v = p.get("modified-after")) so.filter.modified_after = parse_time_option(*v, false, "--modified-after");
  if (auto v = p.get("modified-before")) so.filter.modified_before = parse_time_option(*v, true, "--modified-before");
  if (so.filter.modified_after && so.filter.modified_before &&
      *so.filter.modified_after > *so.filter.modified_before)
    throw UsageError("--modified-after is later than --modified-before");

  so.name_only = p.has("name-only");
  so.exact = p.has("exact");
  so.partial = !p.has("whole-words");
  so.match_any = p.has("any");
  so.reverse = p.has("reverse");
  if (auto v = p.get("sort")) so.sort = parse_sort(*v);
  if (auto v = p.get("limit")) so.limit = parse_count(*v, "--limit");
  else so.limit = 20;

  if (so.query.empty() && so.filter.empty())
    throw UsageError("search needs a query, e.g.: filesearch search \"machine learning\" --type pdf");
  if (p.has("open") && p.has("reveal")) throw UsageError("use either --open or --reveal, not both");
  std::optional<size_t> action_index;
  const char* action_option = p.has("open") ? "--open" : "--reveal";
  if (auto v = p.get("open") ? p.get("open") : p.get("reveal")) {
    const uint64_t n = parse_count(*v, action_option);
    if (n == 0) throw UsageError(std::string(action_option) + " counts from 1");
    action_index = static_cast<size_t>(n);
  }

  IndexStore store(ctx.db_path, OpenMode::MustExist);
  Searcher searcher(store);
  const SearchOutcome outcome = searcher.search(so);

  if (p.has("json")) {
    print_json(ctx, so, outcome);
  } else if (p.has("paths-only")) {
    for (const auto& r : outcome.results) ctx.out << r.file.path << "\n";
  } else {
    print_human(ctx, outcome, p.has("score"));
  }

  if (action_index) {
    if (*action_index > outcome.results.size()) {
      err << "filesearch: there is no result #" << *action_index << " (" << outcome.results.size()
          << " shown)\n";
      return 1;
    }
    const std::string& path = outcome.results[*action_index - 1].file.path;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
      err << "filesearch: " << path << " no longer exists. Run 'filesearch update' to refresh the index.\n";
      return 1;
    }
    std::string problem;
    const bool ok = p.has("open") ? open_file(path, problem) : reveal_file(path, problem);
    if (!ok) {
      err << "filesearch: " << problem << "\n";
      return 1;
    }
  }
  return 0;
}

}  // namespace filesearch::cli
