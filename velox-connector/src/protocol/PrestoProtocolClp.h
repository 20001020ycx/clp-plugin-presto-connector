/*
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

#include <cstdint>
#include <string>

#include "presto_cpp/external/json/nlohmann/json.hpp"
#include "presto_cpp/presto_protocol/core/presto_protocol_core.h"

namespace facebook::presto::protocol::clp {

struct ClpTransactionHandle : public ConnectorTransactionHandle {
  String instance = {};

  ClpTransactionHandle() noexcept { _type = "clp"; }
};

struct ClpColumnHandle : public ColumnHandle {
  String columnName = {};
  String originalColumnName = {};
  Type columnType = {};

  ClpColumnHandle() noexcept { _type = "clp"; }
};

enum class SplitType { ARCHIVE, IR };

struct ClpSplit : public ConnectorSplit {
  String path = {};
  SplitType type = {};
  std::shared_ptr<String> kqlQuery = {};

  ClpSplit() noexcept { _type = "clp"; }
};

struct ClpTableHandle : public ConnectorTableHandle {
  SchemaTableName schemaTableName = {};
  String tablePath = {};

  ClpTableHandle() noexcept { _type = "clp"; }
};

struct ClpTableLayoutHandle : public ConnectorTableLayoutHandle {
    ClpTableHandle table = {};
    std::shared_ptr<String> kqlQuery = {};
    std::shared_ptr<String> metadataFilterQuery = {};

  ClpTableLayoutHandle() noexcept { _type = "clp"; }
};

} // namespace facebook::presto::protocol::clp
