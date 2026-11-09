# FileSearch — High-Performance Local File Search Engine

A lightweight, high-performance local file search engine built using **C++20 and SQLite**. FileSearch indexes files across directories and enables fast, relevance-ranked searches using an inverted index and TF-IDF scoring.

## Overview

FileSearch is a command-line search engine designed to make searching large collections of local files faster and more efficient.

Instead of scanning every file whenever a query is executed, FileSearch builds a persistent index containing file metadata and searchable terms. Queries are processed against this index to retrieve relevant results efficiently.

## Key Features

- **Inverted Indexing:** Maps searchable terms to their associated documents.
- **TF-IDF Ranking:** Ranks search results based on term relevance.
- **Flexible Search:** Supports exact, prefix and substring matching.
- **Incremental Indexing:** Detects added, modified and deleted files.
- **SQLite Storage:** Maintains a persistent index using SQLite.
- **Directory Traversal:** Supports recursive indexing.
- **Hidden File Handling:** Optionally includes hidden files and directories.
- **Symlink Handling:** Supports configurable symbolic-link traversal.
- **Exclude Patterns:** Skips files and directories using glob patterns.
- **Cross-Platform Design:** Includes macOS-specific metadata handling.
- **Automated Testing:** Includes GoogleTest-based test coverage.

## Tech Stack

| Technology | Purpose |
|---|---|
| C++20 | Core implementation |
| SQLite | Persistent index storage |
| CMake | Build system |
| GoogleTest | Unit and integration testing |
| STL | Data structures and filesystem operations |

## Architecture

```text
             User Query
                 |
                 v
          Command Line Interface
                 |
                 v
           Query Processor
                 |
                 v
         Query Term Expansion
                 |
                 v
          Inverted Index
                 |
                 v
          TF-IDF Ranking
                 |
                 v
          Search Results
```

### Indexing Pipeline

```text
      Target Directory
             |
             v
      Directory Walker
             |
             v
       File Metadata
             |
             v
       Term Extraction
             |
             v
       Inverted Index
             |
             v
       SQLite Database
```

## Installation

### Prerequisites

- C++20 compatible compiler
- CMake
- SQLite3
- Git

### Clone the Repository

```bash
git clone https://github.com/AmruthaVarshini111/filesearch-cpp.git

cd filesearch-cpp
```

### Build the Project

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

cmake --build build -j4
```

## Running Tests

Run the complete automated test suite:

```bash
ctest --test-dir build --output-on-failure
```

## Example Usage

Search for Markdown and C++ files:

```bash
filesearch search --type md,cpp --sort size
```

Search results are ranked and displayed with relevant file information.

Additional indexing options include:

- `--hidden` — Include hidden files and directories.
- `--follow-symlinks` — Follow symbolic links.
- `--exclude` — Exclude matching files and directories.

Use:

```bash
filesearch --help
```

to inspect the available commands and options.

## Testing

The project includes automated tests covering:

- Directory traversal
- File indexing and metadata
- Incremental updates
- Hidden files
- Symbolic links
- Search relevance and ranking
- Query filtering
- CLI behaviour
- Database integrity
- Error handling

**Latest verified result: 113/113 tests passed.**

## Project Structure

```text
filesearch-cpp/
├── include/
│   ├── database/
│   ├── indexer/
│   ├── search/
│   └── utils/
├── src/
│   ├── cli/
│   ├── database/
│   ├── indexer/
│   ├── search/
│   ├── utils/
│   └── main.cpp
├── tests/
├── docs/
├── scripts/
├── CMakeLists.txt
├── README.md
├── LICENSE
└── CONTRIBUTING.md
```

## Future Improvements

- Parallel directory indexing
- Advanced query operators
- Search performance benchmarking
- Improved ranking algorithms
- Additional file-format support
- Search result caching

## Author

**Amrutha Varshini Yenugonda**

GitHub: [@AmruthaVarshini111](https://github.com/AmruthaVarshini111)

Project: [FileSearch C++](https://github.com/AmruthaVarshini111/filesearch-cpp)

## License

This project is licensed under the MIT License.
