#include "cli/file_actions.hpp"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

#ifdef __APPLE__
#include <crt_externs.h>
#define FS_ENVIRON (*_NSGetEnviron())
#else
extern char** environ;
#define FS_ENVIRON environ
#endif

namespace filesearch::cli {
namespace {

bool run_program(const std::vector<std::string>& args, std::string& error) {
  std::vector<char*> argv;
  for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int rc = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), FS_ENVIRON);
  if (rc != 0) {
    error = "cannot run '" + args[0] + "': " + std::strerror(rc);
    return false;
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
  error = "'" + args[0] + "' failed";
  return false;
}

}  // namespace

bool open_file(const std::string& path, std::string& error) {
#ifdef __APPLE__
  return run_program({"open", path}, error);
#else
  return run_program({"xdg-open", path}, error);
#endif
}

bool reveal_file(const std::string& path, std::string& error) {
#ifdef __APPLE__
  return run_program({"open", "-R", path}, error);
#else
  (void)path;
  error = "revealing in Finder is only available on macOS";
  return false;
#endif
}

}  // namespace filesearch::cli
