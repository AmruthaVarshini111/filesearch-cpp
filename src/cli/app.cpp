#include <unistd.h>

#include <cstdlib>
#include <iostream>

#include "cli/commands.hpp"
#include "utils/error.hpp"
#include "utils/path_utils.hpp"

namespace filesearch::cli {

std::vector<OptionSpec> common_options(std::vector<OptionSpec> specific) {
  specific.push_back({"db", 0, true});
  specific.push_back({"no-color", 0, false});
  specific.push_back({"help", 'h', false});
  return specific;
}

Context make_context(const ParsedArgs& parsed, std::ostream& out, std::ostream& err,
                     bool allow_progress) {
  const bool no_color_env = std::getenv("NO_COLOR") != nullptr;
  const bool color_ok = !no_color_env && !parsed.has("no-color");
  const bool out_tty = &out == &std::cout && ::isatty(STDOUT_FILENO);
  const bool err_tty = &err == &std::cerr && ::isatty(STDERR_FILENO);

  std::filesystem::path db = pathutil::default_database_path();
  if (auto v = parsed.get("db")) db = pathutil::absolute_normal(*v);

  Context ctx{out, err, db, Terminal(color_ok && out_tty), Terminal(color_ok && err_tty), false, false};
  ctx.quiet = parsed.has("quiet");
  ctx.show_progress = allow_progress && err_tty && !ctx.quiet && !parsed.has("no-progress");
  return ctx;
}

int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
  try {
    if (args.empty()) {
      out << command_help("");
      return 0;
    }
    const std::string& cmd = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    if (cmd == "help" || cmd == "-h" || cmd == "--help") return cmd_help(rest, out, err);
    if (cmd == "version" || cmd == "-V" || cmd == "--version") {
      out << "filesearch " << FILESEARCH_VERSION << "\n";
      return 0;
    }
    if (cmd == "index") return cmd_index(rest, out, err);
    if (cmd == "search") return cmd_search(rest, out, err);
    if (cmd == "stats") return cmd_stats(rest, out, err);
    if (cmd == "update") return cmd_update(rest, out, err);
    if (cmd == "remove") return cmd_remove(rest, out, err);
    if (cmd == "rebuild") return cmd_rebuild(rest, out, err);
    throw UsageError("unknown command '" + cmd + "'");
  } catch (const UsageError& e) {
    err << "filesearch: " << e.what() << "\nRun 'filesearch help' for usage.\n";
    return 2;
  } catch (const MissingDatabaseError& e) {
    err << "filesearch: " << e.what()
        << "\nCreate an index first, for example: filesearch index ~/Documents\n";
    return 1;
  } catch (const CorruptDatabaseError& e) {
    err << "filesearch: the index database is corrupted or unreadable (" << e.what()
        << ").\nRun 'filesearch rebuild' to move it aside and start with a fresh index.\n";
    return 1;
  } catch (const Error& e) {
    err << "filesearch: " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    err << "filesearch: unexpected error: " << e.what() << "\n";
    return 1;
  }
}

}  // namespace filesearch::cli
