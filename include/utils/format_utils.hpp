#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace filesearch::fmt {

/// Parses "1MB", "1.5GB", "500k", "2MiB", "1024". KB/MB/GB/TB (and K/M/G/T)
/// are decimal multiples (matching Finder); KiB/MiB/GiB/TiB are binary.
std::optional<uint64_t> parse_size(std::string_view text);

/// Formats with decimal units, e.g. "1.5 MB".
std::string format_size(uint64_t bytes);

/// Parses a point in time: "YYYY-MM-DD", "today", "yesterday" or a relative
/// age such as "12h", "7d", "2w", "3m" (30 days) and "1y" (365 days). For a
/// calendar date, `end_of_day` selects 23:59:59 instead of 00:00:00. Returns
/// seconds since the Unix epoch, or nullopt when the text is invalid.
std::optional<int64_t> parse_time(std::string_view text, bool end_of_day, int64_t now);

/// "YYYY-MM-DD HH:MM" in local time.
std::string format_time(int64_t epoch_seconds);

/// 1234567 -> "1,234,567".
std::string format_number(uint64_t n);

}  // namespace filesearch::fmt
