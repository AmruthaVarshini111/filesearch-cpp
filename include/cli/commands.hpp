#pragma once
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include "cli/arg_parser.hpp"
#include "database/index_store.hpp"
#include "indexer/indexer.hpp"
#include "utils/terminal.hpp"

#ifndef FILESEARCH_VERSION
#define FILESEARCH_VERSION "1.0.0"
#endif

namespace filesearch::cli {

/// Everything a command needs; streams are injectable so tests can capture output.
struct Context {
  std::ostream& out;
  std::ostream& err;
  std::filesystem::path db_path;
  Terminal term;           // styling for stdout
  Terminal err_term;       // styling for stderr
  bool show_progress = false;
  bool quiet = false;
};

/// Entry point used by main(): returns the process exit code
/// (0 ok, 1 runtime error, 2 usage error).
int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

// Individual commands. `args` excludes the command name itself.
int cmd_index(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
int cmd_search(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
int cmd_stats(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
int cmd_update(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
int cmd_remove(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
int cmd_rebuild(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
int cmd_help(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

// Shared helpers (implemented in app.cpp / cmd_index.cpp).
std::vector<OptionSpec> common_options(std::vector<OptionSpec> specific);
Context make_context(const ParsedArgs& parsed, std::ostream& out, std::ostream& err,
                     bool allow_progress);
std::string command_help(const std::string& command);
IndexReport index_with_feedback(Context& ctx, IndexStore& store, const std::filesystem::path& root,
                                const IndexOptions& options);

}  // namespace filesearch::cli
