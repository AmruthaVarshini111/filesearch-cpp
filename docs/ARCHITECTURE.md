# Architecture

FileSearch is a static library (`filesearch_core`) plus a thin executable. The
library is split into five modules; dependencies point downwards only.

```text
cli  ->  indexer, search  ->  database  ->  (SQLite)
 \_________________________________________/
                    utils
```

| Module | Responsibility |
|---|---|
| `utils/` | ASCII-only string helpers and the tokeniser, size/date parsing and formatting, path helpers, ANSI styling and the progress line, error types. No knowledge of the other modules. |
| `database/` | `Connection`/`Statement` RAII wrappers over the SQLite C API (`sqlite_wrapper`) and `IndexStore`, which owns the schema and every SQL statement. Nothing outside this module writes SQL. |
| `search/` | `extract_terms` (file → terms), `InvertedIndex` (in-memory hash-map index), `rank` / `finalize_score` (TF-IDF), `Searcher` (query pipeline). |
| `indexer/` | `Indexer` + `Walker`: directory traversal and incremental synchronisation of disk and database. |
| `cli/` | Option parser, commands, help text, output formatting, `open`/`reveal`. Streams are injected, so tests drive the whole CLI in-process. |

## Data model

```sql
roots(path PK, include_hidden, follow_symlinks, excludes, last_indexed)
files(id PK, path UNIQUE, name, name_lower, ext, size, mtime, ntok, indexed_at)
      -- indexes: name_lower, ext, size, mtime
postings(term, file_id, tf_name, tf_path, PRIMARY KEY(term, file_id)) WITHOUT ROWID
      -- index: file_id (for deleting a file's postings)
terms(term PK, df) WITHOUT ROWID          -- maintained by triggers on postings
```

- `postings` is the persistent **inverted index**. Because it is `WITHOUT ROWID`
  with `(term, file_id)` as the key, all postings of a term are one contiguous
  range scan.
- `terms.df` (document frequency) is kept up to date by an `AFTER INSERT` and an
  `AFTER DELETE` trigger, so IDF needs no aggregate query. Rows with `df = 0`
  are pruned at the end of each indexing run.
- `PRAGMA user_version` stores the schema version; an unknown version or a
  non-FileSearch SQLite file is reported as a corrupt/incompatible database.
- WAL mode, `synchronous=NORMAL`, and 64 MB page cache are set on open.

## Indexing

1. The root is made absolute and lexically normalised (symlinks are **not**
   resolved, so paths look the way the user typed them).
2. An explicit stack walks directories. `std::filesystem::directory_iterator` is
   used *without* `skip_permission_denied`, because that option silently yields an
   empty listing, which would make the deletion step think files had vanished.
   Errors are collected and the directory is marked unreadable.
3. For each regular file the stored `(size, mtime)` is compared with the disk:
   equal → skip; different → update row and replace postings; unknown → insert.
   Every visited id goes into a `seen` set.
4. Writes are committed every 5,000 changed files.
5. After a complete (not interrupted) walk, rows below the root that were not seen
   and are not below an unreadable directory are deleted together with their
   postings.
6. Symlinks: file links are indexed under the link's path with the target's size
   and mtime; broken links and non-followed directory links are counted and
   skipped. With `--follow-symlinks` each directory is entered once, keyed by its
   canonical path, which also terminates cycles.
7. SIGINT sets a flag; the walk stops, pending writes are committed, the deletion
   step is skipped, exit code 130.

## Searching

```text
query ──► split_words ──► for each word: expand_vocabulary (exact / prefix / substring)
      ──► load postings of all matched terms into an InvertedIndex (hash map)
      ──► rank(): per word best score per file, AND-intersection (or union for --any)
      ──► fetch_files(): file rows for candidates, filters applied in SQL
      ──► finalize_score(): length normalisation and boosts
      ──► sort, apply limit
```

**Tokenisation.** Any ASCII byte that is not a letter or digit separates words;
bytes ≥ 0x80 are letters (UTF-8 survives, but is not case-folded). camelCase
boundaries split words (`PDFReader` → `pdf`, `reader`). At index time the whole
unsplit run is added as an extra term so `filesearch` finds `FileSearch.cpp`.
The file name and its directory are tokenised into separate fields.

**Scoring** (`search/ranker.hpp`). For query word *q* and file *d*:

```text
s(q,d) = max over matched terms t of
         w(t) · idf(t) · [ 3·(1 + ln tf_name)  +  1·(1 + ln tf_path) ]   (a field counts only if tf > 0)
idf(t) = ln(1 + N / df(t))                      w = 1.0 exact, 0.6 prefix, 0.3 substring
score(d) = Σ_q s(q,d)
```

`finalize_score` then divides the name part by `√(words in file name)` and
multiplies by 1.5 if all query words hit the name, by 2.5 if the file stem equals
the query, or by 4 if the full name equals it.

Why TF-IDF: it is simple, explainable, and fits this data. The "documents" are
tiny (a few words), so term frequency is almost always 1 and **IDF does most of
the work**: rare words identify files, common path words (`users`, `documents`)
barely count. Sub-linear TF (`1 + ln tf`) stops repeated words from dominating.
The constants were chosen by reasoning, not tuned against a relevance data set;
unit tests pin the intended orderings (name beats directory, rarer beats common,
exact name first).

**Partial matching.** A word matches vocabulary terms that start with it, and, if
it has at least 3 letters, terms that contain it. Prefix lookups use a range scan
on the `terms` primary key; substring lookups scan the vocabulary with `instr`
(cost grows with the number of distinct terms, see `docs/BENCHMARKS.md`).
`--whole-words` skips expansion.

**Filters** are applied to the candidate rows in SQL: `ext IN (...)`, `path LIKE
'<dir>/%' ESCAPE '\'` (ASCII case-insensitive, wildcards in directory names are
escaped), `size`/`mtime` comparisons. Candidate ids are fetched in chunks of 500
to stay under SQLite's variable limit.

## Error handling

| Situation | Behaviour |
|---|---|
| No database | `MissingDatabaseError`: message with the `index` command to run; `search`/`stats`/`update`/`remove` never create one |
| Not a SQLite file / truncated / foreign schema / unknown version | `CorruptDatabaseError`: message pointing to `rebuild` |
| `rebuild` on a corrupt database | the file and its `-wal`/`-shm` are *renamed* to `*.corrupt-<timestamp>`, never deleted |
| Unreadable directory | warning with the first messages; counted; entries below it are kept |
| Bad option / value | `UsageError`, exit code 2, one-line explanation |
| Result file vanished before `--open` | clear message suggesting `update` |

Exit codes: 0 success, 1 runtime error, 2 usage error, 130 interrupted.

## Security notes

- The tool opens files only to `stat` them; contents are never read.
- Every SQL statement uses bound parameters; no user text is concatenated into
  SQL (only generated placeholders and fixed fragments).
- `open`/`xdg-open` are started with `posix_spawnp` and an argument vector, never
  through a shell, so file names cannot inject commands.
- No network code exists in the project.
- The database lives in the user's own Application Support directory and contains
  file names and paths, so protect it like any list of your file names.

## Testing

`tests/` contains 113 GoogleTest cases: tokeniser and parsers, inverted index
construction, TF-IDF ranking behaviour, SQLite operations (triggers, filters,
persistence, corruption), the indexer (hidden files, duplicates, incremental
add/change/delete, symlink loops, broken links, permission errors, cancellation,
"never modifies files"), search behaviour (matching, filters, sorting) and the CLI
end to end. All tests work in private temporary directories.
