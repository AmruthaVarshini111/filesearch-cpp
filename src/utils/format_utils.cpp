#include "utils/format_utils.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "utils/string_utils.hpp"

namespace filesearch::fmt {

std::optional<uint64_t> parse_size(std::string_view text) {
  const std::string s = strutil::trim(text);
  if (s.empty()) return std::nullopt;
  const unsigned char first = static_cast<unsigned char>(s[0]);
  if (!(first >= '0' && first <= '9') && first != '.') return std::nullopt;
  if (s.size() > 1 && (s[1] == 'x' || s[1] == 'X')) return std::nullopt;  // no hex

  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || errno == ERANGE || !std::isfinite(value)) return std::nullopt;

  const std::string suffix = strutil::to_lower(strutil::trim(end));
  double mult = 0;
  if (suffix.empty() || suffix == "b") mult = 1;
  else if (suffix == "k" || suffix == "kb") mult = 1e3;
  else if (suffix == "m" || suffix == "mb") mult = 1e6;
  else if (suffix == "g" || suffix == "gb") mult = 1e9;
  else if (suffix == "t" || suffix == "tb") mult = 1e12;
  else if (suffix == "kib") mult = 1024.0;
  else if (suffix == "mib") mult = 1024.0 * 1024;
  else if (suffix == "gib") mult = 1024.0 * 1024 * 1024;
  else if (suffix == "tib") mult = 1024.0 * 1024 * 1024 * 1024;
  else return std::nullopt;

  const double bytes = value * mult;
  if (bytes < 0 || bytes >= 1.8e19) return std::nullopt;
  return static_cast<uint64_t>(bytes);
}

std::string format_size(uint64_t bytes) {
  static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1000.0 && u < 4) {
    v /= 1000.0;
    ++u;
  }
  char buf[32];
  if (u == 0)
    std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(bytes));
  else
    std::snprintf(buf, sizeof buf, "%.1f %s", v, units[u]);
  return buf;
}

namespace {
std::tm local_tm(int64_t t) {
  std::tm tm{};
  const std::time_t tt = static_cast<std::time_t>(t);
  localtime_r(&tt, &tm);
  return tm;
}
}  // namespace

std::optional<int64_t> parse_time(std::string_view text, bool end_of_day, int64_t now) {
  const std::string s = strutil::to_lower(strutil::trim(text));
  if (s.empty()) return std::nullopt;

  if (s == "today" || s == "yesterday") {
    std::tm tm = local_tm(now);
    if (s == "yesterday") tm.tm_mday -= 1;
    tm.tm_hour = end_of_day ? 23 : 0;
    tm.tm_min = end_of_day ? 59 : 0;
    tm.tm_sec = end_of_day ? 59 : 0;
    tm.tm_isdst = -1;
    return static_cast<int64_t>(std::mktime(&tm));
  }

  // Relative age: <digits><h|d|w|m|y>
  if (s.size() >= 2) {
    const char unit = s.back();
    bool digits = true;
    for (size_t i = 0; i + 1 < s.size(); ++i)
      if (s[i] < '0' || s[i] > '9') digits = false;
    int64_t unit_secs = 0;
    switch (unit) {
      case 'h': unit_secs = 3600; break;
      case 'd': unit_secs = 86400; break;
      case 'w': unit_secs = 7 * 86400; break;
      case 'm': unit_secs = 30 * 86400; break;
      case 'y': unit_secs = 365 * 86400; break;
      default: break;
    }
    if (digits && unit_secs != 0 && s.size() <= 10)
      return now - std::strtoll(s.c_str(), nullptr, 10) * unit_secs;
  }

  // ISO calendar date.
  int y = 0, m = 0, d = 0;
  if (s.size() == 10 && s[4] == '-' && s[7] == '-' &&
      std::sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) == 3) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = end_of_day ? 23 : 0;
    tm.tm_min = end_of_day ? 59 : 0;
    tm.tm_sec = end_of_day ? 59 : 0;
    tm.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return std::nullopt;
    if (tm.tm_mday != d || tm.tm_mon != m - 1 || tm.tm_year != y - 1900) return std::nullopt;
    return static_cast<int64_t>(t);
  }
  return std::nullopt;
}

std::string format_time(int64_t epoch_seconds) {
  const std::tm tm = local_tm(epoch_seconds);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
  return buf;
}

std::string format_number(uint64_t n) {
  std::string digits = std::to_string(n), out;
  int count = 0;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    if (count && count % 3 == 0) out.push_back(',');
    out.push_back(*it);
    ++count;
  }
  return std::string(out.rbegin(), out.rend());
}

}  // namespace filesearch::fmt
