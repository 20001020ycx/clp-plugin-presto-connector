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

#include <glog/logging.h>
#include <sstream>

#include "clp_s/search/ast/ConvertToExists.hpp"
#include "clp_s/search/ast/EmptyExpr.hpp"
#include "clp_s/search/ast/NarrowTypes.hpp"
#include "clp_s/search/ast/OrOfAndForm.hpp"
#include "clp_s/search/kql/kql.hpp"
#include "connector/search_lib/BaseClpCursor.h"

using namespace clp_s;
using namespace clp_s::search;
using namespace clp_s::search::ast;

namespace facebook::velox::connector::clp::search_lib {

void BaseClpCursor::executeQuery(
    const std::string& query,
    const std::vector<Field>& outputColumns) {
  query_ = query;
  outputColumns_ = outputColumns;
  errorCode_ = preprocessQuery();
}

ErrorCode BaseClpCursor::preprocessQuery() {
  auto queryStream = std::istringstream(query_);
  expr_ = kql::parse_kql_expression(queryStream);
  if (nullptr == expr_) {
    VLOG(2) << "Failed to parse query '" << query_ << "'";
    return ErrorCode::InvalidQuerySyntax;
  }

  if (std::dynamic_pointer_cast<EmptyExpr>(expr_)) {
    VLOG(2) << "Query '" << query_ << "' is logically false";
    return ErrorCode::LogicalError;
  }

  OrOfAndForm standardizePass;
  if (expr_ = standardizePass.run(expr_);
      std::dynamic_pointer_cast<EmptyExpr>(expr_)) {
    VLOG(2) << "Query '" << query_ << "' is logically false";
    return ErrorCode::LogicalError;
  }

  NarrowTypes narrowPass;
  if (expr_ = narrowPass.run(expr_);
      std::dynamic_pointer_cast<EmptyExpr>(expr_)) {
    VLOG(2) << "Query '" << query_ << "' is logically false";
    return ErrorCode::LogicalError;
  }

  ConvertToExists convertPass;
  if (expr_ = convertPass.run(expr_);
      std::dynamic_pointer_cast<EmptyExpr>(expr_)) {
    VLOG(2) << "Query '" << query_ << "' is logically false";
    return ErrorCode::LogicalError;
  }

  return ErrorCode::Success;
}

} // namespace facebook::velox::connector::clp::search_lib
