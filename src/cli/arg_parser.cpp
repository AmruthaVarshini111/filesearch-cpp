#include "cli/arg_parser.hpp"

#include "utils/error.hpp"

namespace filesearch::cli {

std::optional<std::string> ParsedArgs::get(const std::string& name) const {
  auto it = values_.find(name);
  if (it == values_.end() || it->second.empty()) return std::nullopt;
  return it->second.back();
}

std::vector<std::string> ParsedArgs::get_all(const std::string& name) const {
  auto it = values_.find(name);
  return it == values_.end() ? std::vector<std::string>{} : it->second;
}

ParsedArgs parse_args(const std::vector<std::string>& args, const std::vector<OptionSpec>& specs) {
  ParsedArgs out;
  auto find_long = [&](const std::string& n) -> const OptionSpec* {
    for (const auto& s : specs)
      if (s.name == n) return &s;
    return nullptr;
  };
  auto find_short = [&](char c) -> const OptionSpec* {
    for (const auto& s : specs)
      if (s.short_name == c) return &s;
    return nullptr;
  };

  bool options_done = false;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (options_done || a.size() < 2 || a[0] != '-') {
      out.positional_.push_back(a);
      continue;
    }
    if (a == "--") {
      options_done = true;
      continue;
    }

    const OptionSpec* spec = nullptr;
    std::optional<std::string> inline_value;
    std::string shown = a;
    if (a[1] == '-') {
      std::string name = a.substr(2);
      const auto eq = name.find('=');
      if (eq != std::string::npos) {
        inline_value = name.substr(eq + 1);
        name = name.substr(0, eq);
      }
      shown = "--" + name;
      spec = find_long(name);
    } else {
      if (a.size() != 2) throw UsageError("unknown option '" + a + "'");
      spec = find_short(a[1]);
    }
    if (!spec) throw UsageError("unknown option '" + shown + "'");

    if (spec->takes_value) {
      if (inline_value) {
        out.values_[spec->name].push_back(*inline_value);
      } else {
        if (i + 1 >= args.size()) throw UsageError("option '" + shown + "' needs a value");
        out.values_[spec->name].push_back(args[++i]);
      }
    } else {
      if (inline_value) throw UsageError("option '" + shown + "' does not take a value");
      out.values_[spec->name];  // present, no values
    }
  }
  return out;
}

}  // namespace filesearch::cli
