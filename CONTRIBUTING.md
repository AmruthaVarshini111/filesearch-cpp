# Contributing to FileSearch

Thanks for your interest! Bug reports, ideas and pull requests are welcome.

## Getting started

```bash
git clone <your-fork-url> filesearch && cd filesearch
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Before opening a pull request, also run the sanitizer build:

```bash
cmake -S . -B build-san -DFILESEARCH_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-san -j && ctest --test-dir build-san --output-on-failure
```

## Guidelines

- **C++20, no extra dependencies.** The standard library and SQLite3 only; GoogleTest for tests. Keep the code compiling with both Apple clang and GCC without warnings (`-Wall -Wextra -Wpedantic`).
- **Never touch user files.** FileSearch reads the file system; it must not write, move or delete anything outside its own database. Tests must use temporary directories (`TempDir` in `tests/test_helpers.hpp`), never real paths, and never the real index.
- **Add tests** for every behaviour change; bug fixes get a regression test.
- **Keep modules separate:** `cli/` talks to users, `indexer/` to the file system, `database/` to SQLite, `search/` implements ranking. `utils/` has no knowledge of the others.
- **Claims need measurements.** Do not describe a change as faster or better without numbers from `scripts/benchmark.sh` (or a test) and a description of the machine.
- **Documentation:** update the README and `docs/` when options or behaviour change, and keep examples real.
- Keep commits focused; use short imperative commit messages ("Add --follow-symlinks cycle test").

## Reporting bugs

Please include your macOS version, compiler (`c++ --version`), the exact command, and the output of `filesearch stats`. Do not paste private file names you want to keep out of the issue.

## Code of conduct

Be kind and constructive. Harassment or personal attacks are not tolerated.
