cmake_minimum_required(VERSION 3.16)
project(FileSearch VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FILESEARCH_BUILD_TESTS "Build the unit tests (needs GoogleTest)" ON)
option(FILESEARCH_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

# SQLite ships with macOS (and with the Xcode command line tools SDK).
find_package(SQLite3 REQUIRED)

set(FILESEARCH_SOURCES
  src/cli/app.cpp
  src/cli/arg_parser.cpp
  src/cli/cmd_index.cpp
  src/cli/cmd_search.cpp
  src/cli/cmd_stats.cpp
  src/cli/file_actions.cpp
  src/cli/help.cpp
  src/database/index_store.cpp
  src/database/sqlite_wrapper.cpp
  src/indexer/indexer.cpp
  src/search/inverted_index.cpp
  src/search/ranker.cpp
  src/search/searcher.cpp
  src/search/term_extractor.cpp
  src/utils/format_utils.cpp
  src/utils/path_utils.cpp
  src/utils/string_utils.cpp
  src/utils/terminal.cpp
)

add_library(filesearch_core STATIC ${FILESEARCH_SOURCES})
target_include_directories(filesearch_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(filesearch_core PUBLIC SQLite::SQLite3)
target_compile_definitions(filesearch_core PUBLIC FILESEARCH_VERSION="${PROJECT_VERSION}")
if(MSVC)
  target_compile_options(filesearch_core PRIVATE /W4)
else()
  target_compile_options(filesearch_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(FILESEARCH_SANITIZE)
  target_compile_options(filesearch_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(filesearch_core PUBLIC -fsanitize=address,undefined)
endif()

add_executable(filesearch src/main.cpp)
target_link_libraries(filesearch PRIVATE filesearch_core)

install(TARGETS filesearch RUNTIME DESTINATION bin)

if(FILESEARCH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
