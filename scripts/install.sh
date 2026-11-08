#!/usr/bin/env bash
# FileSearch installer: builds from source and installs into a user-local
# directory. No administrator privileges are needed.
#
#   ./scripts/install.sh [--prefix DIR] [--run-tests] [--add-to-path] [--yes]
#
# Defaults: installs the binary to ~/.local/bin/filesearch.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
PREFIX="${HOME}/.local"
RUN_TESTS=0
ADD_TO_PATH=0

usage() {
  cat <<EOF
Usage: scripts/install.sh [options]

  --prefix DIR     Install under DIR (binary goes to DIR/bin). Default: \$HOME/.local
  --run-tests      Build and run the unit tests before installing (needs GoogleTest;
                   on macOS: brew install googletest - otherwise it is downloaded)
  --add-to-path    Append a PATH line to your shell profile if DIR/bin is not on PATH
  -h, --help       Show this help
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) [ $# -ge 2 ] || { echo "error: --prefix needs a value" >&2; exit 2; }; PREFIX="$2"; shift 2 ;;
    --run-tests) RUN_TESTS=1; shift ;;
    --add-to-path) ADD_TO_PATH=1; shift ;;
    --yes|-y) shift ;;  # accepted for compatibility; the installer never asks questions
    -h|--help) usage; exit 0 ;;
    *) echo "error: unknown option '$1'" >&2; usage >&2; exit 2 ;;
  esac
done

say()  { printf '==> %s\n' "$*"; }
fail() { printf 'error: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- dependencies
say "Checking dependencies"
missing=0
OS="$(uname -s)"
[ "$OS" = "Darwin" ] || echo "note: this project targets macOS; building on $OS (the 'reveal in Finder' action will be unavailable)."

CXX_BIN=""
for c in "${CXX:-}" c++ clang++ g++; do
  if [ -n "$c" ] && command -v "$c" >/dev/null 2>&1; then CXX_BIN="$c"; break; fi
done
if [ -z "$CXX_BIN" ]; then
  echo "  missing: a C++20 compiler"
  echo "    macOS: run  xcode-select --install   (installs Apple clang and the SQLite headers)"
  missing=1
else
  echo "  compiler : $CXX_BIN ($("$CXX_BIN" --version 2>&1 | head -n 1))"
fi

if command -v cmake >/dev/null 2>&1; then
  CMAKE_VER="$(cmake --version | head -n 1 | sed 's/[^0-9.]*//')"
  CMAKE_MAJOR="${CMAKE_VER%%.*}"; REST="${CMAKE_VER#*.}"; CMAKE_MINOR="${REST%%.*}"
  if [ "$CMAKE_MAJOR" -lt 3 ] || { [ "$CMAKE_MAJOR" -eq 3 ] && [ "$CMAKE_MINOR" -lt 16 ]; }; then
    echo "  too old : cmake $CMAKE_VER (need >= 3.16). Upgrade with: brew upgrade cmake"
    missing=1
  else
    echo "  cmake    : $CMAKE_VER"
  fi
else
  echo "  missing: cmake   ->  brew install cmake   (Homebrew: https://brew.sh)"
  missing=1
fi

if command -v make >/dev/null 2>&1 || command -v ninja >/dev/null 2>&1; then
  echo "  build tool: $(command -v ninja >/dev/null 2>&1 && echo ninja || echo make)"
else
  echo "  missing: make or ninja (installed with the Xcode command line tools)"
  missing=1
fi
# SQLite3 (headers + library) is verified by CMake's find_package at configure time.

if [ "$missing" -ne 0 ]; then
  fail "install the missing dependencies above, then run this script again. Nothing was changed."
fi

# ----------------------------------------------------------------------- build
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2)"
TEST_FLAG=OFF
[ "$RUN_TESTS" -eq 1 ] && TEST_FLAG=ON

say "Configuring (Release) in $BUILD"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DFILESEARCH_BUILD_TESTS="$TEST_FLAG" >/dev/null \
  || fail "CMake configuration failed. If it mentions SQLite3, install the Xcode command line tools (xcode-select --install) or: brew install sqlite"

say "Building with $JOBS parallel jobs"
cmake --build "$BUILD" -j "$JOBS" >/dev/null || fail "build failed; re-run 'cmake --build build' to see the compiler output"

if [ "$RUN_TESTS" -eq 1 ]; then
  say "Running unit tests"
  (cd "$BUILD" && ctest --output-on-failure) || fail "unit tests failed; nothing was installed"
fi

# --------------------------------------------------------------------- install
say "Installing to $PREFIX/bin"
cmake --install "$BUILD" --prefix "$PREFIX" >/dev/null || fail "could not install into $PREFIX (check permissions)"
BIN="$PREFIX/bin/filesearch"
"$BIN" --version >/dev/null || fail "installed binary does not start: $BIN"
echo "  installed: $BIN ($("$BIN" --version))"

# ------------------------------------------------------------------------ PATH
on_path=0
case ":$PATH:" in *":$PREFIX/bin:"*) on_path=1 ;; esac

shell_rc() {
  case "$(basename "${SHELL:-/bin/zsh}")" in
    zsh)  echo "$HOME/.zshrc" ;;
    bash) if [ "$OS" = "Darwin" ]; then echo "$HOME/.bash_profile"; else echo "$HOME/.bashrc"; fi ;;
    *)    echo "" ;;
  esac
}

if [ "$on_path" -eq 1 ]; then
  say "$PREFIX/bin is already on your PATH - try:  filesearch help"
else
  RC="$(shell_rc)"
  LINE="export PATH=\"$PREFIX/bin:\$PATH\""
  if [ "$ADD_TO_PATH" -eq 1 ] && [ -n "$RC" ]; then
    if grep -q '# >>> filesearch >>>' "$RC" 2>/dev/null; then
      say "PATH entry already present in $RC"
    else
      { echo ""; echo "# >>> filesearch >>>"; echo "$LINE"; echo "# <<< filesearch <<<"; } >> "$RC"
      say "Added $PREFIX/bin to PATH in $RC"
    fi
    echo "  Open a new terminal window, or run:  source \"$RC\""
  else
    say "$PREFIX/bin is not on your PATH yet. To use 'filesearch' from any directory, run:"
    echo ""
    echo "    echo '$LINE' >> ${RC:-~/.zshrc} && source ${RC:-~/.zshrc}"
    echo ""
    echo "  (or re-run this script with --add-to-path to do it for you)"
  fi
fi
say "Done. Next:  filesearch index ~/Documents   then   filesearch search <words>"
