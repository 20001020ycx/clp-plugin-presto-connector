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
#include <string_view>

namespace facebook::velox::config {
class ConfigBase;
} // namespace facebook::velox::config

namespace facebook::velox::connector::clp {

class ClpS3AuthProviderBase {
 public:
  explicit ClpS3AuthProviderBase(
      std::shared_ptr<const config::ConfigBase> config)
      : config_(config) {}
  virtual ~ClpS3AuthProviderBase() = default;

  /// Constructs the actual S3 URL so that CLP-s can access the split.
  ///
  /// @param splitPath The current path stored in the split to add.
  /// @return The constructed S3 URL.
  virtual std::string constructS3Url(std::string_view splitPath) = 0;

  /// Exports the three environment variables needed by CLP-s
  /// to system:
  ///   AWS_ACCESS_KEY_ID
  ///   AWS_SECRET_ACCESS_KEY
  ///   AWS_SESSION_TOKEN (optional)
  /// So that at runtime CLP-S's code can authenticate with S3.
  ///
  /// @return Whether environment variable export succeeded or not.
  virtual bool exportAuthEnvironmentVariables() = 0;

 protected:
  /// Sets an environment variable for different OS, then gets the environment
  /// variable to do a sanity check.
  ///
  /// @param key The environment variable name to set.
  /// @param value The environment variable value to set.
  /// @return Whether environment variable export succeeded or not.
  static void setEnvironmentVariable(
      std::string_view key,
      std::string_view value);

  /// Unsets an environment variable for different OS, then gets the environment
  /// variable to do the sanity check.
  ///
  /// @param key The environment variable name to unset.
  static void unsetEnvironmentVariable(std::string_view key);

  std::shared_ptr<const config::ConfigBase> config_;
};

} // namespace facebook::velox::connector::clp
