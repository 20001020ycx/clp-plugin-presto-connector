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

#include "ClpPrestoToVeloxConnector.h"
#include "presto_cpp/main/types/TypeParser.h"
#include "connector/ClpColumnHandle.h"
#include "connector/ClpConnectorSplit.h"
#include "connector/ClpTableHandle.h"
#include "velox/common/encode/Base64.h"

namespace facebook::presto {

using namespace velox;

std::unique_ptr<velox::connector::ConnectorSplit>
ClpPrestoToVeloxConnector::toVeloxSplit(
        const protocol::ConnectorId& catalogId,
        const protocol::ConnectorSplit* connectorSplit,
        const protocol::SplitContext* /*splitContext*/) const {
    auto clpSplit = dynamic_cast<const protocol::clp::ClpSplit*>(connectorSplit);
    VELOX_CHECK_NOT_NULL(
            clpSplit, "Unexpected split type {}", connectorSplit->_type);
    return std::make_unique<connector::clp::ClpConnectorSplit>(
            catalogId,
            clpSplit->path,
            static_cast<int>(clpSplit->type),
            clpSplit->kqlQuery);
}

std::unique_ptr<velox::connector::ColumnHandle>
ClpPrestoToVeloxConnector::toVeloxColumnHandle(
    const protocol::ColumnHandle* column,
    const TypeParser& typeParser) const {
  auto clpColumn = dynamic_cast<const protocol::clp::ClpColumnHandle*>(column);
  VELOX_CHECK_NOT_NULL(
      clpColumn, "Unexpected column handle type {}", column->_type);
  return std::make_unique<connector::clp::ClpColumnHandle>(
      clpColumn->columnName,
      clpColumn->originalColumnName,
      typeParser.parse(clpColumn->columnType));
}

std::unique_ptr<velox::connector::ConnectorTableHandle>
ClpPrestoToVeloxConnector::toVeloxTableHandle(
    const protocol::TableHandle& tableHandle,
    const VeloxExprConverter& /*exprConverter*/,
    const TypeParser& /*typeParser*/) const {
  auto clpLayout =
      std::dynamic_pointer_cast<const protocol::clp::ClpTableLayoutHandle>(
          tableHandle.connectorTableLayout);
  VELOX_CHECK_NOT_NULL(
      clpLayout,
      "Unexpected layout type {}",
      tableHandle.connectorTableLayout->_type);
  return std::make_unique<connector::clp::ClpTableHandle>(
      tableHandle.connectorId, clpLayout->table.schemaTableName.table);
}

std::unique_ptr<protocol::ConnectorProtocol>
ClpPrestoToVeloxConnector::createConnectorProtocol() const {
  return std::make_unique<protocol::clp::ClpConnectorProtocol>();
}

} // namespace facebook::presto
