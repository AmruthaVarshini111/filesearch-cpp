#!/usr/bin/env bash
# Measures indexing and search speed on a synthetic tree of empty files.
#   ./scripts/benchmark.sh [N_FILES] [path/to/filesearch]
# Everything happens in a temporary directory that is deleted afterwards.
# Results depend on your machine; do not compare numbers across machines.
set -euo pipefail

N="${1:-20000}"
BIN="${2:-${FILESEARCH_BIN:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/build/filesearch}}"
[ -x "$BIN" ] || { echo "error: $BIN not found; build first (cmake --build build)" >&2; exit 1; }

WORK="$(mktemp -d "${TMPDIR:-/tmp}/filesearch-bench.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
TREE="$WORK/tree"
DB="$WORK/bench.db"
mkdir -p "$TREE"

echo "Generating $N empty files..."
d=0
while [ $d -lt 100 ]; do
  s=0
  while [ $s -lt 7 ]; do mkdir -p "$TREE/dir$d/sub$s"; s=$((s+1)); done
  d=$((d+1))
done
awk -v n="$N" -v root="$TREE" 'BEGIN {
  srand(42)
  split("report invoice notes machine learning summary photo draft final budget thesis lecture resume project data archive backup meeting plan review", w, " ")
  split("pdf txt md csv jpg png docx xlsx cpp py", e, " ")
  for (i = 0; i < n; i++)
    printf "%s/dir%d/sub%d/%s_%s_%d.%s\n", root, int(rand()*100), i % 7, w[int(rand()*20)+1], w[int(rand()*20)+1], i, e[int(rand()*10)+1]
}' | xargs touch

time_line() { sed -n 's/^  Time      \([0-9.]*\) s.*/\1/p'; }

echo "Full index..."
full="$("$BIN" index "$TREE" --db "$DB" --no-progress | time_line)"
echo "Incremental re-index (nothing changed)..."
incr="$("$BIN" index "$TREE" --db "$DB" --no-progress | time_line)"
touch "$TREE/dir1/sub1/brand_new_file.txt"
echo "Incremental re-index (1 new file)..."
incr1="$("$BIN" update --db "$DB" --no-progress | time_line)"

median_ms() {  # run a query 9 times, print the median of the engine-reported time
  local out=""
  for _ in 1 2 3 4 5 6 7 8 9; do
    out="$out$("$BIN" search "$@" --json --db "$DB" | sed -n 's/.*"elapsed_ms":\([0-9.]*\).*/\1/p')
"
  done
  printf '%s' "$out" | sort -n | sed -n '5p'
}

echo
echo "| Measurement | Result |"
echo "|---|---|"
echo "| Files indexed | $N |"
echo "| Full index | ${full} s |"
echo "| Re-index, no changes | ${incr} s |"
echo "| Update after adding 1 file | ${incr1} s |"
echo "| Search 'invoice' (common word) | $(median_ms invoice) ms |"
echo "| Search 'machine learning' (2 words) | $(median_ms machine learning) ms |"
echo "| Search 'archive' --type pdf | $(median_ms archive --type pdf) ms |"
echo "| Search 'learn' (prefix/substring) | $(median_ms learn) ms |"
echo "| Search 'invoice' --whole-words (no vocabulary scan) | $(median_ms invoice --whole-words) ms |"
echo "| Search 'brand new' (2 words, 1 match) | $(median_ms brand new) ms |"
echo "| Database size | $("$BIN" stats --db "$DB" | sed -n 's/.*Database  *[^(]*(\(.*\))/\1/p') |"
echo
echo "Search times are the engine's own measurement (median of 9 runs, excludes process start-up)."
echo "Machine: $(uname -sm)"
