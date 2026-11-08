#pragma once
#include <chrono>
#include <ostream>
#include <string>
#include <string_view>

namespace filesearch {

/// Minimal ANSI styling. When colour is disabled every method returns its
/// input unchanged, so output stays clean when piped or under NO_COLOR.
class Terminal {
 public:
  explicit Terminal(bool color = false) : color_(color) {}
  bool color() const { return color_; }
  std::string bold(std::string_view s) const { return wrap("1", s); }
  std::string dim(std::string_view s) const { return wrap("2", s); }
  std::string red(std::string_view s) const { return wrap("31", s); }
  std::string green(std::string_view s) const { return wrap("32", s); }
  std::string yellow(std::string_view s) const { return wrap("33", s); }
  std::string cyan(std::string_view s) const { return wrap("36", s); }

 private:
  std::string wrap(const char* code, std::string_view s) const {
    if (!color_) return std::string(s);
    return std::string("\033[") + code + "m" + std::string(s) + "\033[0m";
  }
  bool color_;
};

/// A single self-overwriting status line (used for indexing progress).
class ProgressLine {
 public:
  ProgressLine(std::ostream& os, bool enabled) : os_(os), enabled_(enabled) {}
  ~ProgressLine() { finish(); }
  void update(const std::string& text);
  void finish();

 private:
  std::ostream& os_;
  bool enabled_;
  size_t last_width_ = 0;
  std::chrono::steady_clock::time_point last_ = {};
};

}  // namespace filesearch
