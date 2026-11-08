#include "utils/string_utils.hpp"

#include <cstdio>

namespace filesearch::strutil {
namespace {

constexpr size_t kMaxTermBytes = 64;

bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
bool is_upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
bool is_lower(unsigned char c) { return c >= 'a' && c <= 'z'; }
bool is_word_byte(unsigned char c) {
  return is_digit(c) || is_upper(c) || is_lower(c) || c >= 0x80;
}
char lower_char(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Maximal runs of word bytes, original case.
std::vector<std::string_view> word_runs(std::string_view text) {
  std::vector<std::string_view> runs;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && !is_word_byte(static_cast<unsigned char>(text[i]))) ++i;
    size_t start = i;
    while (i < text.size() && is_word_byte(static_cast<unsigned char>(text[i]))) ++i;
    if (i > start) runs.push_back(text.substr(start, i - start));
  }
  return runs;
}

// Splits one run on camelCase boundaries and lower-cases the parts.
std::vector<std::string> camel_parts(std::string_view run) {
  std::vector<std::string> parts;
  std::string cur;
  for (size_t i = 0; i < run.size(); ++i) {
    const auto c = static_cast<unsigned char>(run[i]);
    if (i > 0) {
      const auto prev = static_cast<unsigned char>(run[i - 1]);
      bool boundary = false;
      if (is_upper(c) && (is_lower(prev) || is_digit(prev))) {
        boundary = true;  // fooBar, foo2Bar
      } else if (is_upper(c) && is_upper(prev) && i + 1 < run.size() &&
                 is_lower(static_cast<unsigned char>(run[i + 1]))) {
        boundary = true;  // PDFReader -> PDF | Reader
      }
      if (boundary && !cur.empty()) {
        parts.push_back(std::move(cur));
        cur.clear();
      }
    }
    cur.push_back(lower_char(run[i]));
  }
  if (!cur.empty()) parts.push_back(std::move(cur));
  return parts;
}

}  // namespace

std::string to_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = lower_char(c);
  return out;
}

std::string trim(std::string_view s) {
  size_t b = 0, e = s.size();
  auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
  while (b < e && ws(s[b])) ++b;
  while (e > b && ws(s[e - 1])) --e;
  return std::string(s.substr(b, e - b));
}

std::vector<std::string> split(std::string_view s, char delim) {
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    size_t pos = s.find(delim, start);
    if (pos == std::string_view::npos) {
      out.emplace_back(s.substr(start));
      break;
    }
    out.emplace_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (lower_char(a[i]) != lower_char(b[i])) return false;
  return true;
}

bool glob_match(std::string_view pattern, std::string_view text) {
  const std::string p = to_lower(pattern), t = to_lower(text);
  size_t pi = 0, ti = 0, star = std::string::npos, mark = 0;
  while (ti < t.size()) {
    if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) {
      ++pi;
      ++ti;
    } else if (pi < p.size() && p[pi] == '*') {
      star = pi++;
      mark = ti;
    } else if (star != std::string::npos) {
      pi = star + 1;
      ti = ++mark;
    } else {
      return false;
    }
  }
  while (pi < p.size() && p[pi] == '*') ++pi;
  return pi == p.size();
}

std::vector<std::string> split_words(std::string_view text) {
  std::vector<std::string> out;
  for (auto run : word_runs(text))
    for (auto& part : camel_parts(run)) out.push_back(std::move(part));
  return out;
}

std::vector<std::string> index_terms(std::string_view text) {
  std::vector<std::string> out;
  for (auto run : word_runs(text)) {
    auto parts = camel_parts(run);
    const bool split = parts.size() > 1;
    for (auto& p : parts)
      if (p.size() <= kMaxTermBytes) out.push_back(std::move(p));
    if (split && run.size() <= kMaxTermBytes) out.push_back(to_lower(run));
  }
  return out;
}

std::string escape_like(std::string_view s) {
  std::string out;
  for (char c : s) {
    if (c == '\\' || c == '%' || c == '_') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

std::string json_escape(std::string_view s) {
  std::string out;
  for (char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(ch);
        }
    }
  }
  return out;
}

}  // namespace filesearch::strutil
