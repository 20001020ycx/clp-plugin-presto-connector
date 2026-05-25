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

#include "presto_cpp/external/json/nlohmann/json.hpp"
#include "protocol/PrestoProtocolClp.h"
#include "presto_cpp/presto_protocol/core/presto_protocol_core.h"
#include "presto_cpp/presto_protocol/core/ConnectorProtocol.h"

namespace facebook::presto::protocol::clp {

    class ClpConnectorProtocol : public ConnectorProtocol {
    public:
        // --- ConnectorTableHandle ---
        void to_json(json& j, const std::shared_ptr<ConnectorTableHandle>& proto)
        const override {
          VELOX_NYI("to_json not implemented");
        }
        void from_json(const json& j, std::shared_ptr<ConnectorTableHandle>& proto)
        const override {
            VELOX_NYI("from_json not implemented");
        }
        void serialize(
                const std::shared_ptr<ConnectorTableHandle>& proto,
                std::string& thrift) const override {
          VELOX_NYI("Serialize not implemented");
        }
        void deserialize(
                const std::string& binaryData,
                std::shared_ptr<ConnectorTableHandle>& proto) const override;

        // --- ConnectorTableLayoutHandle ---
        void to_json(
                json& j,
                const std::shared_ptr<ConnectorTableLayoutHandle>& proto) const override {
          VELOX_NYI("to_json not implemented");
        }
        void from_json(
                const json& j,
                std::shared_ptr<ConnectorTableLayoutHandle>& proto) const override {
            VELOX_NYI("from_json not implemented");
        }
        void serialize(
                const std::shared_ptr<ConnectorTableLayoutHandle>& proto,
                std::string& thrift) const override {
          VELOX_NYI("Serialize not implemented");
        }
        void deserialize(
                const std::string& binaryData,
                std::shared_ptr<ConnectorTableLayoutHandle>& proto) const override;

        // --- ColumnHandle ---
        void to_json(json& j, const std::shared_ptr<ColumnHandle>& proto)
        const override {
          VELOX_NYI("to_json not implemented");
        }
        void from_json(const json& j, std::shared_ptr<ColumnHandle>& proto)
        const override {
            VELOX_NYI("from_json not implemented");
        }
        void serialize(
                const std::shared_ptr<ColumnHandle>& proto,
                std::string& thrift) const override {
          VELOX_NYI("Serialize not implemented");
        }
        void deserialize(
                const std::string& binaryData,
                std::shared_ptr<ColumnHandle>& proto) const override;

        // --- ConnectorInsertTableHandle ---
        void to_json(json& j, const std::shared_ptr<ConnectorInsertTableHandle>& proto)
        const override {
          VELOX_NYI("ConnectorInsertTableHandle not implemented");
        }
        void from_json(const json& j, std::shared_ptr<ConnectorInsertTableHandle>& proto)
        const override {
          VELOX_NYI("ConnectorInsertTableHandle not implemented");
        }
        void serialize(
                const std::shared_ptr<ConnectorInsertTableHandle>& proto,
                std::string& thrift) const override {
          VELOX_NYI("ConnectorInsertTableHandle not implemented");
        }
        void deserialize(
                const std::string& thrift,
                std::shared_ptr<ConnectorInsertTableHandle>& proto) const override {
          VELOX_NYI("ConnectorInsertTableHandle not implemented");
        }

        // --- ConnectorDistributedProcedureHandle ---
        void to_json(
                json& j,
                const std::shared_ptr<ConnectorDistributedProcedureHandle>& proto)
        const override {
          VELOX_NYI("ConnectorDistributedProcedureHandle not implemented");
        }
        void from_json(
                const json& j,
                std::shared_ptr<ConnectorDistributedProcedureHandle>& proto)
        const override {
          VELOX_NYI("ConnectorDistributedProcedureHandle not implemented");
        }

        // --- ConnectorOutputTableHandle ---
        void to_json(json& j, const std::shared_ptr<ConnectorOutputTableHandle>& proto)
        const override {
          VELOX_NYI("ConnectorOutputTableHandle not implemented");
        }
        void from_json(const json& j, std::shared_ptr<ConnectorOutputTableHandle>& proto)
        const override {
          VELOX_NYI("ConnectorOutputTableHandle not implemented");
        }
        void serialize(
                const std::shared_ptr<ConnectorOutputTableHandle>& proto,
                std::string& thrift) const override {
          VELOX_NYI("ConnectorOutputTableHandle not implemented");
        }
        void deserialize(
                const std::string& thrift,
                std::shared_ptr<ConnectorOutputTableHandle>& proto) const override {
          VELOX_NYI("ConnectorOutputTableHandle not implemented");
        }

        // --- ConnectorSplit ---
        void to_json(json& j, const std::shared_ptr<ConnectorSplit>& proto)
        const override {
          VELOX_NYI("to_json not implemented");
        }
        void from_json(const json& j, std::shared_ptr<ConnectorSplit>& proto)
        const override {
            VELOX_NYI("from_json not implemented");
        }
        void serialize(
                const std::shared_ptr<ConnectorSplit>& proto,
                std::string& thrift) const override {
          VELOX_NYI("Serialize not implemented");
        }
        void deserialize(
                const std::string& binaryData,
                std::shared_ptr<ConnectorSplit>& proto) const override;

        // --- ConnectorPartitioningHandle ---
        void to_json(json& j, const std::shared_ptr<ConnectorPartitioningHandle>& proto)
        const override {
          VELOX_NYI("ConnectorPartitioningHandle not implemented");
        }
        void from_json(
                const json& j,
                std::shared_ptr<ConnectorPartitioningHandle>& proto) const override {
          VELOX_NYI("ConnectorPartitioningHandle not implemented");
        }
        void serialize(
                const std::shared_ptr<ConnectorPartitioningHandle>& proto,
                std::string& thrift) const override {
          VELOX_NYI("ConnectorPartitioningHandle not implemented");
        }
        void deserialize(
                const std::string& thrift,
                std::shared_ptr<ConnectorPartitioningHandle>& proto) const override {
          VELOX_NYI("ConnectorPartitioningHandle not implemented");
        }

        // --- ConnectorTransactionHandle ---
        void to_json(json& j, const std::shared_ptr<ConnectorTransactionHandle>& proto)
        const override {
          VELOX_NYI("to_json not implemented");
        }
        void from_json(const json& j, std::shared_ptr<ConnectorTransactionHandle>& proto)
        const override {
            VELOX_NYI("from_json not implemented");
        }
        void serialize(
                const std::shared_ptr<ConnectorTransactionHandle>& proto,
                std::string& thrift) const override {
          VELOX_NYI("Serialize not implemented");
        }
        void deserialize(
                const std::string& binaryData,
                std::shared_ptr<ConnectorTransactionHandle>& proto) const override;

        // --- ConnectorDeleteTableHandle ---
        void to_json(json& j, const std::shared_ptr<ConnectorDeleteTableHandle>& proto)
        const override {
          VELOX_NYI("ConnectorDeleteTableHandle not implemented");
        }
        void from_json(const json& j, std::shared_ptr<ConnectorDeleteTableHandle>& proto)
        const override {
          VELOX_NYI("ConnectorDeleteTableHandle not implemented");
        }
        void serialize(
                const std::shared_ptr<ConnectorDeleteTableHandle>& proto,
                std::string& thrift) const override {
          VELOX_NYI("ConnectorDeleteTableHandle not implemented");
        }
        void deserialize(
                const std::string& thrift,
                std::shared_ptr<ConnectorDeleteTableHandle>& proto) const override {
          VELOX_NYI("ConnectorDeleteTableHandle not implemented");
        }

        // --- ConnectorIndexHandle ---
        void to_json(json& j, const std::shared_ptr<ConnectorIndexHandle>& proto)
        const override {
          VELOX_NYI("ConnectorIndexHandle not implemented");
        }
        void from_json(const json& j, std::shared_ptr<ConnectorIndexHandle>& proto)
        const override {
          VELOX_NYI("ConnectorIndexHandle not implemented");
        }
        void serialize(
                const std::shared_ptr<ConnectorIndexHandle>& proto,
                std::string& thrift) const override {
          VELOX_NYI("ConnectorIndexHandle not implemented");
        }
        void deserialize(
                const std::string& thrift,
                std::shared_ptr<ConnectorIndexHandle>& proto) const override {
          VELOX_NYI("ConnectorIndexHandle not implemented");
        }
    };

} // namespace facebook::presto::protocol::clp
