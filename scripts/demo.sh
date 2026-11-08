#!/usr/bin/env bash
# Runs a short demonstration against a throw-away copy of the sample dataset.
# Uses a temporary database, so your real index and files are not involved.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${1:-${FILESEARCH_BIN:-$ROOT/build/filesearch}}"
[ -x "$BIN" ] || BIN="$(command -v filesearch || true)"
[ -x "$BIN" ] || { echo "error: build first (cmake --build build) or install filesearch" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/filesearch-demo.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
cp -R "$ROOT/tests/sample_data" "$WORK/sample"
export FILESEARCH_DB="$WORK/demo.db"

run() { printf '\n$ filesearch %s\n' "$*"; "$BIN" "$@"; }
run index "$WORK/sample"
run search "machine learning"
run search "machine learning" --type pdf
run search report --name-only --sort name
run search --type md,cpp --sort size
run stats
