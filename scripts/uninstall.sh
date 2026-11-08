#!/usr/bin/env bash
# Removes the FileSearch binary and (optionally) its index database.
# Your own files are never touched.
#
#   ./scripts/uninstall.sh [--prefix DIR] [--purge-data] [--yes]
set -euo pipefail

PREFIX="${HOME}/.local"
PURGE=0
ASSUME_YES=0

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) [ $# -ge 2 ] || { echo "error: --prefix needs a value" >&2; exit 2; }; PREFIX="$2"; shift 2 ;;
    --purge-data) PURGE=1; shift ;;
    --yes|-y) ASSUME_YES=1; shift ;;
    -h|--help)
      echo "Usage: scripts/uninstall.sh [--prefix DIR] [--purge-data] [--yes]"
      echo "  --purge-data  also delete the index database (re-creatable with 'filesearch index')"
      exit 0 ;;
    *) echo "error: unknown option '$1'" >&2; exit 2 ;;
  esac
done

say() { printf '==> %s\n' "$*"; }

BIN="$PREFIX/bin/filesearch"
if [ -f "$BIN" ]; then
  rm -f "$BIN"
  say "Removed $BIN"
else
  say "No binary at $BIN (already removed?)"
fi

# PATH block written by install.sh --add-to-path (only that marked block is removed).
for rc in "$HOME/.zshrc" "$HOME/.bash_profile" "$HOME/.bashrc"; do
  if [ -f "$rc" ] && grep -q '# >>> filesearch >>>' "$rc"; then
    tmp="$(mktemp)"
    awk '/# >>> filesearch >>>/{skip=1} !skip{print} /# <<< filesearch <<</{skip=0}' "$rc" > "$tmp"
    cat "$tmp" > "$rc"
    rm -f "$tmp"
    say "Removed the FileSearch PATH entry from $rc"
  fi
done

if [ "$(uname -s)" = "Darwin" ]; then
  DATA_DIR="$HOME/Library/Application Support/FileSearch"
else
  DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/filesearch"
fi

if [ "$PURGE" -eq 1 ]; then
  if [ -d "$DATA_DIR" ]; then
    if [ "$ASSUME_YES" -ne 1 ]; then
      printf 'Delete the FileSearch index database in "%s"? [y/N] ' "$DATA_DIR"
      read -r answer || answer=n
      case "$answer" in y|Y|yes|YES) ;; *) say "Kept the database."; exit 0 ;; esac
    fi
    # Delete only files FileSearch created, never the whole directory tree.
    rm -f "$DATA_DIR"/index.db "$DATA_DIR"/index.db-wal "$DATA_DIR"/index.db-shm "$DATA_DIR"/index.db.corrupt-*
    rmdir "$DATA_DIR" 2>/dev/null || true
    say "Removed the index database"
  else
    say "No database directory found at $DATA_DIR"
  fi
else
  [ -d "$DATA_DIR" ] && say "Kept the index database in \"$DATA_DIR\" (use --purge-data to delete it)."
fi
say "FileSearch is uninstalled."
