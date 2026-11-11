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

#include "ClpHostCompatibility.h"

#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <glog/logging.h>

#include "ClpVtableLayout.h"
#include "presto_cpp/main/connectors/PrestoToVeloxConnector.h"
#include "velox/common/base/Exceptions.h"
#include "velox/connectors/Connector.h"

namespace facebook::presto {

namespace {

constexpr std::string_view kPrestoGitTag{CLP_PLUGIN_PRESTO_GIT_TAG};

/// Compares the number of vtable entries that presto_server has for
/// `Interface` with the number that this plugin was compiled with.
///
/// @tparam Interface
/// @param name The qualified name of `Interface`.
/// @param mismatches Returns a description of the difference appended to it,
/// if the numbers differ.
/// @param unverified Returns `name` appended to it, if presto_server's vtable
/// can't be found.
template <typename Interface>
void checkInterface(
    std::string_view name,
    std::vector<std::string>& mismatches,
    std::vector<std::string>& unverified) {
  auto const optionalNumExportedEntries{
      numExportedVtableEntries(typeid(Interface))};
  if (false == optionalNumExportedEntries.has_value()) {
    unverified.emplace_back(name);
    return;
  }
  auto const numCompiledEntries{numCompiledVtableEntries<Interface>()};
  if (optionalNumExportedEntries.value() != numCompiledEntries) {
    mismatches.emplace_back(
        fmt::format(
            "the vtable of {} has {} entries in presto_server and {} in the"
            " plugin",
            name,
            optionalNumExportedEntries.value(),
            numCompiledEntries));
  }
}

} // namespace

void verifyHostCompatibility() {
  std::vector<std::string> mismatches;
  std::vector<std::string> unverified;
  checkInterface<PrestoToVeloxConnector>(
      "facebook::presto::PrestoToVeloxConnector", mismatches, unverified);
  checkInterface<velox::connector::Connector>(
      "facebook::velox::connector::Connector", mismatches, unverified);
  checkInterface<velox::connector::ConnectorFactory>(
      "facebook::velox::connector::ConnectorFactory", mismatches, unverified);
  checkInterface<velox::connector::DataSource>(
      "facebook::velox::connector::DataSource", mismatches, unverified);
  checkInterface<velox::connector::ConnectorSplit>(
      "facebook::velox::connector::ConnectorSplit", mismatches, unverified);
  checkInterface<velox::connector::ConnectorTableHandle>(
      "facebook::velox::connector::ConnectorTableHandle",
      mismatches,
      unverified);
  checkInterface<velox::connector::ColumnHandle>(
      "facebook::velox::connector::ColumnHandle", mismatches, unverified);

  if (false == mismatches.empty()) {
    VELOX_USER_FAIL(
        "The CLP connector plugin is incompatible with this presto_server: the"
        " plugin was built against Presto {}, but presto_server was built from"
        " different sources ({}). Use a presto-native image built from that"
        " Presto commit, or a plugin built for this presto_server.",
        kPrestoGitTag,
        fmt::join(mismatches, "; "));
  }
  if (false == unverified.empty()) {
    LOG(WARNING) << fmt::format(
        "CLP connector plugin (built against Presto {}): couldn't check all of"
        " presto_server's connector interfaces, since no vtable was found for"
        " {}.",
        kPrestoGitTag,
        fmt::join(unverified, ", "));
    return;
  }
  LOG(INFO) << fmt::format(
      "CLP connector plugin (built against Presto {}): presto_server's"
      " connector interfaces have as many virtual functions as the plugin's.",
      kPrestoGitTag);
}

} // namespace facebook::presto
