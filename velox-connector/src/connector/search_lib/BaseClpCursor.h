/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "clp_s/InputConfig.hpp"
#include "connector/ClpConnectorSplit.h"

namespace clp_s {
class BaseColumnReader;
} // namespace clp_s

namespace clp_s::search::ast {
class Expression;
} // namespace clp_s::search::ast

namespace facebook::velox::connector::clp::search_lib {

enum class ErrorCode {
  DictionaryNotFound,
  InternalError,
  InvalidQuerySyntax,
  InvalidTimestampRange,
  LogicalError,
  QueryNotInitialized,
  SchemaNotFound,
  Success
};

enum class ColumnType {
  Array,
  Boolean,
  Float,
  Integer,
  String,
  Timestamp,
  Unknown = -1
};

struct Field {
  ColumnType type;
  std::string name;
};

/// A query execution interface that manages the lifecycle of a query on a CLP-S
/// split (archive or IR), including parsing and validating the query, loading
/// the relevant splits, applying filters, and iterating over the results. It
/// abstracts away the low-level details of split access
/// while supporting projection and batch-oriented retrieval of filtered rows.
class BaseClpCursor {
 public:
  explicit BaseClpCursor(
      clp_s::InputSource inputSource,
      std::string_view splitPath)
      : errorCode_(ErrorCode::QueryNotInitialized),
        inputSource_(inputSource),
        splitPath_(std::string(splitPath)) {}
  virtual ~BaseClpCursor() = default;

  /// Executes a query. This function parses, validates, and prepares the given
  /// query for execution.
  ///
  /// @param query The KQL query to execute.
  /// @param outputColumns A vector specifying the columns to be included in the
  /// query result.
  void executeQuery(
      const std::string& query,
      const std::vector<Field>& outputColumns);

  /// Fetches the next set of rows from the cursor. If the split is not yet
  /// loaded, this function will perform the necessary loading.
  ///
  /// @param numRows The maximum number of rows to fetch.
  /// @return The number of rows scanned.
  virtual uint64_t fetchNext(uint64_t numRows) = 0;

  /// Gets the count of rows that satisfy the query (used to size the result
  /// vector).
  ///
  /// @return Count of rows matching the query.
  virtual size_t getNumFilteredRows() const = 0;

  /// Creates a Vector of the specified type and size.
  ///
  /// This method recursively creates vectors for complex types like ROW. For
  /// primitive types, it creates a LazyVector that will load the data from the
  /// underlying data source when it is accessed.
  ///
  /// @param pool The memory pool used by ClpDataSource to create the vector
  /// @param vectorType
  /// @param vectorSize
  /// @return A Vector of the specified type and size.
  virtual VectorPtr createVector(
      memory::MemoryPool* pool,
      const TypePtr& vectorType,
      size_t vectorSize) = 0;

 protected:
  /// Loads the split from archive or IR stream.
  ///
  /// @return The error code.
  virtual ErrorCode loadSplit() = 0;

  bool currentSplitLoaded_{false};
  ErrorCode errorCode_;
  std::shared_ptr<clp_s::search::ast::Expression> expr_;
  clp_s::InputSource inputSource_;
  std::vector<Field> outputColumns_;
  std::string query_;
  std::string splitPath_;

 private:
  /// Preprocesses the query, performing parsing, validation, and optimization.
  ///
  /// @return The error code.
  ErrorCode preprocessQuery();
};

} // namespace facebook::velox::connector::clp::search_lib
