#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace filesearch::pathutil {

std::string home_dir();

/// Expands a leading "~" or "~/".
std::filesystem::path expand_user(std::string_view p);

/// Expands "~", makes the path absolute (relative to the current directory)
/// and normalises "." / ".." lexically. Symlinks are NOT resolved, so paths
/// are shown the way the user typed them. No trailing slash (except "/").
std::filesystem::path absolute_normal(const std::filesystem::path& p);

/// Location of the index database. Order: $FILESEARCH_DB, then
/// ~/Library/Application Support/FileSearch/index.db (macOS) or
/// $XDG_DATA_HOME/filesearch/index.db (other platforms).
std::filesystem::path default_database_path();

bool is_hidden_name(const std::string& name);

/// "dir" -> "dir/" ("/" stays "/").
std::string dir_prefix(const std::string& dir);

/// Smallest string greater than every string starting with `prefix`
/// (prefix with its last byte incremented). Used for index range scans.
std::string prefix_upper_bound(std::string prefix);

/// True when `path` is `dir` itself or lies below it.
bool is_same_or_inside(const std::string& path, const std::string& dir);

/// Parent directory of an absolute path string ("/a/b" -> "/a", "/a" -> "/").
std::string parent_of(const std::string& path);

/// "dir/name" without doubling the separator for "/".
std::string join(const std::string& dir, const std::string& name);

}  // namespace filesearch::pathutil
