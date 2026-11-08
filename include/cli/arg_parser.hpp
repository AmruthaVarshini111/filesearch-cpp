#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace filesearch::cli {

struct OptionSpec {
  std::string name;        // long name without the leading dashes
  char short_name = 0;     // optional one-letter alias
  bool takes_value = false;
};

class ParsedArgs {
 public:
  const std::vector<std::string>& positional() const { return positional_; }
  bool has(const std::string& name) const { return values_.count(name) != 0; }
  /// Last value given for an option (nullopt when absent or a flag).
  std::optional<std::string> get(const std::string& name) const;
  /// Every value given for a repeatable option, in order.
  std::vector<std::string> get_all(const std::string& name) const;

 private:
  friend ParsedArgs parse_args(const std::vector<std::string>&, const std::vector<OptionSpec>&);
  std::vector<std::string> positional_;
  std::map<std::string, std::vector<std::string>> values_;
};

/// Parses "--name value", "--name=value", "-n value", flags and "--" (end of
/// options). Throws UsageError for unknown options or missing values.
ParsedArgs parse_args(const std::vector<std::string>& args, const std::vector<OptionSpec>& specs);

}  // namespace filesearch::cli
