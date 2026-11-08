#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace filesearch::strutil {

/// ASCII-only lower casing. Bytes >= 0x80 (UTF-8) are left untouched.
std::string to_lower(std::string_view s);
std::string trim(std::string_view s);
std::vector<std::string> split(std::string_view s, char delim);
std::string join(const std::vector<std::string>& parts, std::string_view sep);
bool iequals(std::string_view a, std::string_view b);

/// Case-insensitive glob match supporting '*' and '?'.
bool glob_match(std::string_view pattern, std::string_view text);

/// Splits text into lower-case words. Any ASCII byte that is not a letter or
/// digit is a separator, and camelCase boundaries are split as well
/// ("PDFReader2" -> "pdf", "reader2"). Bytes >= 0x80 are treated as letters so
/// UTF-8 file names stay searchable.
std::vector<std::string> split_words(std::string_view text);

/// Terms that are written to the index for a piece of text: every word from
/// split_words() plus, for runs that were split, the whole lower-cased run
/// ("FileSearch" -> "file", "search", "filesearch"). Duplicates are kept so
/// callers can compute term frequencies.
std::vector<std::string> index_terms(std::string_view text);

/// Escapes %, _ and \ for use in a SQL LIKE ... ESCAPE '\' pattern.
std::string escape_like(std::string_view s);

/// Escapes a string for inclusion inside a JSON string literal.
std::string json_escape(std::string_view s);

}  // namespace filesearch::strutil
