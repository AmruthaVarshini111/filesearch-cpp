#include <map>

#include "cli/commands.hpp"
#include "utils/error.hpp"

namespace filesearch::cli {
namespace {

const char* kGeneral = R"(FileSearch - fast local file search (inverted index + SQLite)

USAGE
  filesearch <command> [options]

COMMANDS
  index <directory>...   Scan directories and add them to the index
  search <query>         Search the index
  stats                  Show index statistics
  update                 Re-scan all indexed directories (incremental)
  remove <directory>     Remove a directory from the index (files are never touched)
  rebuild                Throw away the index contents and re-scan every directory
  help [command]         Show help

EXAMPLES
  filesearch index ~/Documents
  filesearch search "machine learning" --type pdf
  filesearch search resume --directory ~/Documents
  filesearch search report --min-size 1MB --sort size
  filesearch search notes --open 1

GLOBAL OPTIONS (accepted by every command)
  --db <path>            Use a different database (default: see below; or set $FILESEARCH_DB)
  --no-color             Disable colours (also honours the NO_COLOR variable)
  -h, --help             Show help for a command
  -V, --version          Print the version

The index is stored at ~/Library/Application Support/FileSearch/index.db on
macOS. FileSearch only ever reads your files; it never modifies, moves or
deletes them.
)";

const char* kIndex = R"(USAGE
  filesearch index <directory>... [options]

Recursively scans the directories and stores file names, paths, extensions,
sizes and modification times, plus a searchable term index. Running it again
on a directory is incremental: unchanged files are skipped, new and modified
files are added, deleted files are dropped.

OPTIONS
  --hidden               Include hidden files and directories (names starting with '.')
  --follow-symlinks      Descend into symlinked directories (loops are detected)
  --exclude <glob>       Skip entries whose name matches, e.g. node_modules or "*.log"
                         (repeatable)
  -q, --quiet            Print nothing except errors
  --no-progress          Do not show the live progress line
)";

const char* kSearch = R"(USAGE
  filesearch search <query> [options]

Words in the query are combined with AND and matched case-insensitively
against file names and directory names. A word also matches the beginning of
a longer word and, from 3 letters on, any part of one ("learn" finds
"machine-learning.pdf"). Results are ranked with TF-IDF; hits in the file name
count more than hits in the directory path.

FILTERS
  -t, --type <ext>       Only these extensions, e.g. pdf or pdf,docx (repeatable)
  -d, --directory <dir>  Only files below this directory
  --min-size <size>      Minimum size: 500, 10KB, 1MB, 2GB, 1MiB (KB/MB/GB are decimal)
  --max-size <size>      Maximum size
  --modified-after <t>   Modified on/after: 2024-01-31, today, yesterday, 7d, 2w, 3m, 1y
  --modified-before <t>  Modified on/before (a calendar date includes the whole day)

MATCHING
  --name-only            Match the file name only, ignore directory names
  --exact                The file name must equal the query (e.g. "report.pdf")
  --whole-words          Disable prefix/substring matching
  --any                  Match files containing ANY word instead of all

OUTPUT
  -n, --limit <n>        Number of results (default 20, 0 = all)
  -s, --sort <key>       relevance (default), size, date or name
  --reverse              Reverse the sort order
  -p, --paths-only       Print only absolute paths, one per line
  --json                 Machine-readable output
  --score                Show the relevance score

ACTIONS (never modify files)
  --open <n>             Open the n-th result with the default application
  --reveal <n>           Reveal the n-th result in Finder (macOS)

A query may be empty when a filter is given: filesearch search --type pdf
lists PDFs. Use "--" before a query that starts with a dash.
)";

const char* kStats = R"(USAGE
  filesearch stats [options]

Shows how many files are indexed, their total size, the number of distinct
terms and postings, the most common file types, the database size and the
indexed directories.

OPTIONS
  --verify               Also run SQLite's integrity check on the database
)";

const char* kUpdate = R"(USAGE
  filesearch update [options]

Re-scans every indexed directory using the options it was indexed with
(--hidden, --follow-symlinks, --exclude). Only changes are written.

OPTIONS
  -q, --quiet            Print nothing except errors
  --no-progress          Do not show the live progress line
)";

const char* kRemove = R"(USAGE
  filesearch remove <directory>

Removes the directory (and everything below it) from the index. The files on
disk are not touched. If the directory lies inside a larger indexed
directory, 'filesearch update' will add it back unless you index the larger
directory with --exclude.
)";

const char* kRebuild = R"(USAGE
  filesearch rebuild [options]

Deletes the contents of the index and re-scans every indexed directory from
scratch. If the database file is corrupted it is moved aside (never deleted)
and a fresh one is created; index your directories again afterwards.

OPTIONS
  -q, --quiet            Print nothing except errors
  --no-progress          Do not show the live progress line
)";

}  // namespace

std::string command_help(const std::string& command) {
  static const std::map<std::string, const char*> table = {
      {"index", kIndex}, {"search", kSearch}, {"stats", kStats},
      {"update", kUpdate}, {"remove", kRemove}, {"rebuild", kRebuild}};
  if (command.empty()) return kGeneral;
  auto it = table.find(command);
  if (it == table.end()) throw UsageError("no help available for '" + command + "'");
  return it->second;
}

int cmd_help(const std::vector<std::string>& args, std::ostream& out, std::ostream&) {
  out << command_help(args.empty() ? std::string() : args[0]);
  return 0;
}

}  // namespace filesearch::cli
