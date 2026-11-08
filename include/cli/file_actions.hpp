#pragma once
#include <string>

namespace filesearch::cli {

/// Opens a file with the default application (macOS `open`, `xdg-open` elsewhere).
/// The program is started directly (no shell), so file names cannot inject
/// commands. Never modifies the file. Returns false and fills `error` on failure.
bool open_file(const std::string& path, std::string& error);

/// Reveals a file in Finder (`open -R`). macOS only.
bool reveal_file(const std::string& path, std::string& error);

}  // namespace filesearch::cli
