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

// Free of Velox, so `tests/` can build it outside the build-env image. See
// tests/README.md.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace facebook::velox::connector::clp::search_lib {

/// Formats a clp-s archive format version, packed as in an archive header
/// (major in bits 24-31, minor in bits 16-23, patch in bits 0-15), as
/// "major.minor.patch".
///
/// @param version
/// @return The formatted version.
inline std::string formatClpArchiveVersion(uint32_t version) {
  return std::to_string(version >> 24) + "." +
      std::to_string((version >> 16) & 0xFF) + "." +
      std::to_string(version & 0xFFFF);
}

/// Checks whether this connector can read an archive. The archive format is
/// backward compatible, so the connector reads any archive whose version is no
/// newer than the one its CLP reader writes.
///
/// @param archivePath
/// @param archiveVersion The packed version from the archive's header.
/// @param maxSupportedVersion The packed version written by the connector's CLP
/// reader.
/// @return An error message naming both versions if the archive is newer than
/// the connector supports, or std::nullopt otherwise.
inline std::optional<std::string> checkClpArchiveVersion(
    std::string_view archivePath,
    uint32_t archiveVersion,
    uint32_t maxSupportedVersion) {
  if (archiveVersion <= maxSupportedVersion) {
    return std::nullopt;
  }
  return "Archive '" + std::string{archivePath} +
      "' uses clp-s archive format version " +
      formatClpArchiveVersion(archiveVersion) +
      ", but this connector only reads versions up to " +
      formatClpArchiveVersion(maxSupportedVersion) +
      ". Upgrade the connector to read this archive.";
}

} // namespace facebook::velox::connector::clp::search_lib
