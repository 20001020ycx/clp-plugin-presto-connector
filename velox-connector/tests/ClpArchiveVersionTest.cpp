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

#include "connector/search_lib/ClpArchiveVersion.h"

#include <gtest/gtest.h>

namespace facebook::velox::connector::clp::search_lib {
namespace {

constexpr uint32_t makeVersion(uint32_t major, uint32_t minor, uint32_t patch) {
  return (major << 24) | (minor << 16) | patch;
}

constexpr uint32_t kSupportedVersion = makeVersion(0, 7, 0);

TEST(ClpArchiveVersionTest, formatsEachComponent) {
  EXPECT_EQ(formatClpArchiveVersion(makeVersion(0, 7, 0)), "0.7.0");
  EXPECT_EQ(formatClpArchiveVersion(makeVersion(1, 255, 65535)), "1.255.65535");
}

TEST(ClpArchiveVersionTest, acceptsOlderAndEqualVersions) {
  EXPECT_FALSE(
      checkClpArchiveVersion("a", makeVersion(0, 3, 0), kSupportedVersion));
  EXPECT_FALSE(
      checkClpArchiveVersion("a", makeVersion(0, 6, 9), kSupportedVersion));
  EXPECT_FALSE(
      checkClpArchiveVersion("a", kSupportedVersion, kSupportedVersion));
}

// A patch bump can add a feature (0.4.1 added float formats), so it is rejected
// too.
TEST(ClpArchiveVersionTest, rejectsNewerPatchVersion) {
  EXPECT_TRUE(
      checkClpArchiveVersion("a", makeVersion(0, 7, 1), kSupportedVersion));
}

TEST(ClpArchiveVersionTest, errorNamesArchiveAndBothVersions) {
  EXPECT_EQ(
      checkClpArchiveVersion(
          "/data/abc.clps", makeVersion(0, 8, 0), kSupportedVersion),
      "Archive '/data/abc.clps' uses clp-s archive format version 0.8.0, "
      "but this connector only reads versions up to 0.7.0. Upgrade the "
      "connector to read this archive.");
}

} // namespace
} // namespace facebook::velox::connector::clp::search_lib
