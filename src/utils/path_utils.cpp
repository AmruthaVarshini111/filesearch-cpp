#include "utils/path_utils.hpp"

#include <pwd.h>
#include <unistd.h>

#include <cstdlib>
#include <system_error>

namespace filesearch::pathutil {
namespace fs = std::filesystem;

std::string home_dir() {
  const char* h = std::getenv("HOME");
  if (h && *h) return h;
  if (const passwd* pw = getpwuid(getuid())) return pw->pw_dir;
  return {};
}

fs::path expand_user(std::string_view p) {
  if (p == "~") return home_dir();
  if (p.size() >= 2 && p[0] == '~' && p[1] == '/') return home_dir() + std::string(p.substr(1));
  return fs::path(std::string(p));
}

fs::path absolute_normal(const fs::path& p) {
  fs::path e = expand_user(p.string());
  std::error_code ec;
  fs::path a = fs::absolute(e, ec);
  if (ec) a = e;
  std::string s = a.lexically_normal().string();
  while (s.size() > 1 && s.back() == '/') s.pop_back();
  return fs::path(s);
}

fs::path default_database_path() {
  if (const char* env = std::getenv("FILESEARCH_DB"); env && *env) return fs::path(env);
#ifdef __APPLE__
  return fs::path(home_dir()) / "Library" / "Application Support" / "FileSearch" / "index.db";
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
    return fs::path(xdg) / "filesearch" / "index.db";
  return fs::path(home_dir()) / ".local" / "share" / "filesearch" / "index.db";
#endif
}

bool is_hidden_name(const std::string& name) {
  return !name.empty() && name[0] == '.' && name != "." && name != "..";
}

std::string dir_prefix(const std::string& dir) {
  if (!dir.empty() && dir.back() == '/') return dir;
  return dir + "/";
}

std::string prefix_upper_bound(std::string prefix) {
  if (prefix.empty()) return prefix;
  prefix.back() = static_cast<char>(prefix.back() + 1);
  return prefix;
}

bool is_same_or_inside(const std::string& path, const std::string& dir) {
  if (path == dir) return true;
  const std::string prefix = dir_prefix(dir);
  return path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
}

std::string parent_of(const std::string& path) {
  const auto pos = path.rfind('/');
  if (pos == std::string::npos) return ".";
  if (pos == 0) return "/";
  return path.substr(0, pos);
}

std::string join(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir.back() == '/') return dir + name;
  return dir + "/" + name;
}

}  // namespace filesearch::pathutil
