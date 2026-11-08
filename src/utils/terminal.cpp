#include "utils/terminal.hpp"

namespace filesearch {

void ProgressLine::update(const std::string& text) {
  if (!enabled_) return;
  const auto now = std::chrono::steady_clock::now();
  if (last_width_ != 0 && now - last_ < std::chrono::milliseconds(100)) return;
  last_ = now;
  std::string line = text;
  if (line.size() > 100) line = line.substr(0, 97) + "...";
  os_ << '\r' << line;
  if (last_width_ > line.size()) os_ << std::string(last_width_ - line.size(), ' ');
  os_ << std::flush;
  last_width_ = line.size();
}

void ProgressLine::finish() {
  if (!enabled_ || last_width_ == 0) return;
  os_ << '\r' << std::string(last_width_, ' ') << '\r' << std::flush;
  last_width_ = 0;
}

}  // namespace filesearch
