#pragma once
#include <stdexcept>
#include <string>

namespace filesearch {

/// Base class for all errors that are reported to the user.
class Error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// Bad command line usage (unknown option, missing argument, ...).
class UsageError : public Error {
 public:
  using Error::Error;
};

class DatabaseError : public Error {
 public:
  using Error::Error;
};

/// The database file exists but cannot be read as a FileSearch index.
class CorruptDatabaseError : public DatabaseError {
 public:
  using DatabaseError::DatabaseError;
};

/// No index has been created yet.
class MissingDatabaseError : public DatabaseError {
 public:
  using DatabaseError::DatabaseError;
};

}  // namespace filesearch
