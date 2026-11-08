# Benchmarks

`scripts/benchmark.sh [N_FILES] [path/to/filesearch]` generates N empty files in a
temporary directory (100 directories × 7 subdirectories; names are two random
words from a list of 20, a unique number and one of 10 extensions), then times:

- a full index
- a re-index with nothing changed
- an `update` after adding one file
- several searches (median of 9 runs, taken from the engine's own `elapsed_ms`,
  so process start-up is excluded)

The temporary directory and database are deleted afterwards.

## Reference run

- 100,000 files, CMake Release build, GCC 13, Ubuntu 24.04
  Linux x86_64 virtual machine, **1 CPU core**, temporary directory on the VM's disk.
- This is **not** a macOS measurement. Run the script on your Mac for numbers that
  apply to you; do not compare figures between machines.

| Measurement | Result |
|---|---|
| Full index | 7.42 s |
| Re-index, no changes | 0.45 s |
| Update after adding 1 file | 0.44 s |
| Search `invoice` (common word, about 10% of files) | 38.9 ms |
| Search `machine learning` (2 words) | 21.2 ms |
| Search `archive` with `--type pdf` | 21.9 ms |
| Search `learn` (prefix and substring expansion) | 38.8 ms |
| Search `invoice` with `--whole-words` | 33.0 ms |
| Search `brand new` (2 words, 1 match) | 12.1 ms |
| Database size | 75.1 MB |

## What the numbers do and do not say

- Indexing is incremental by design; the "no changes" run is dominated by walking
  the tree and comparing size and mtime for each file.
- `--whole-words` skips scanning the vocabulary of distinct terms, which saved
  about 6 ms for `invoice` here. This synthetic tree has many distinct terms (one
  unique number per file) so this is a vocabulary-heavy case, but it is also a
  tree of empty files with short names; real directories differ.
- Search time here grows with the number of matching files (the common word is
  slower than the 1-match query). Nothing in these runs measured *why*; profile
  before optimising.
- The database is about 750 bytes per file because every file stores postings for
  the words in its directory path as well as its name.
- Only one data set size was measured; scaling to millions of files has not been
  tested.
