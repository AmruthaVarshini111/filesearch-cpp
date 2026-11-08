#include <iostream>
#include <string>
#include <vector>

#include "cli/commands.hpp"

int main(int argc, char** argv) {
  std::ios::sync_with_stdio(false);
  std::vector<std::string> args(argv + 1, argv + argc);
  const int rc = filesearch::cli::run(args, std::cout, std::cerr);
  std::cout.flush();
  return rc;
}
