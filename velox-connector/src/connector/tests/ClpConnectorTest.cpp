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

#include <folly/init/Init.h>
#include <gtest/gtest.h>

#include "velox/common/base/Fs.h"
#include "connector/ClpColumnHandle.h"
#include "connector/ClpConnector.h"
#include "connector/ClpConnectorSplit.h"
#include "connector/ClpTableHandle.h"
#include "velox/exec/tests/utils/AssertQueryBuilder.h"
#include "velox/exec/tests/utils/OperatorTestBase.h"
#include "velox/exec/tests/utils/PlanBuilder.h"
#include "velox/type/Timestamp.h"
#include "velox/type/Type.h"

namespace {

using namespace facebook::velox;
using namespace facebook::velox::connector::clp;

using facebook::velox::exec::test::PlanBuilder;

// Epoch seconds and nanoseconds for the timestamp "2025-04-30T08:50:05Z"
constexpr int64_t kTestTimestampSeconds{1746003005};
constexpr uint64_t kTestTimestampNanoseconds{0ULL};

class ClpConnectorTest : public exec::test::OperatorTestBase {
 public:
  const std::string kClpConnectorId = "test-clp";

  void SetUp() override {
    OperatorTestBase::SetUp();
    connector::clp::ClpConnectorFactory factory;
    auto clpConnector = factory.newConnector(
        kClpConnectorId,
        std::make_shared<const config::ConfigBase>(
            std::unordered_map<std::string, std::string>{}),
        nullptr,
        nullptr);
    connector::registerConnector(clpConnector);
  }

  void TearDown() override {
    connector::unregisterConnector(kClpConnectorId);
    OperatorTestBase::TearDown();
  }

  exec::Split makeClpSplit(
      const std::string& splitPath,
      ClpConnectorSplit::SplitType type,
      std::shared_ptr<std::string> kqlQuery) {
    return exec::Split(
        std::make_shared<ClpConnectorSplit>(
            kClpConnectorId, splitPath, static_cast<int>(type), kqlQuery));
  }

  RowVectorPtr getResults(
      const core::PlanNodePtr& planNode,
      std::vector<exec::Split>&& splits) {
    return exec::test::AssertQueryBuilder(planNode)
        .splits(std::move(splits))
        .copyResults(pool());
  }

  static std::string getExampleFilePath(const std::string& filePath) {
    std::string current_path = fs::current_path().string();
    return current_path + "/examples/" + filePath;
  }
};

TEST_F(ClpConnectorTest, test1NoPushdown) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder()
          .startTableScan()
          .outputType(
              ROW({"requestId", "userId", "method"},
                  {VARCHAR(), VARCHAR(), VARCHAR()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_1"))
          .assignments({
              {"requestId",
               std::make_shared<ClpColumnHandle>(
                   "requestId", "requestId", VARCHAR())},
              {"userId",
               std::make_shared<ClpColumnHandle>(
                   "userId", "userId", VARCHAR())},
              {"method",
               std::make_shared<ClpColumnHandle>(
                   "method", "method", VARCHAR())},
          })
          .endTableScan()
          .filter("method = 'GET'")
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_1.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// requestId
       makeFlatVector<StringView>(
           {"req-100", "req-105", "req-107", "req-109", "req-102"}),
       // userId
       makeNullableFlatVector<StringView>(
           {"user201", "user204", "user202", "user203", std::nullopt}),
       // method
       makeFlatVector<StringView>({
           "GET",
           "GET",
           "GET",
           "GET",
           "GET",
       })});
  test::assertEqualVectors(expected, output);

  // The IR stream will be deserialized in order, so the expected vector is
  // different
  auto irExpected = makeRowVector(
      {// requestId
       makeFlatVector<StringView>(
           {"req-100", "req-102", "req-105", "req-107", "req-109"}),
       // userId
       makeNullableFlatVector<StringView>(
           {"user201", std::nullopt, "user204", "user202", "user203"}),
       // method
       makeFlatVector<StringView>({
           "GET",
           "GET",
           "GET",
           "GET",
           "GET",
       })});
  auto irOutput = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_1_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery)});
  test::assertEqualVectors(irExpected, irOutput);
}

TEST_F(ClpConnectorTest, test1Pushdown) {
  auto kqlQuery =
      std::make_shared<std::string>("method: \"POST\" AND status: 200");
  auto plan =
      PlanBuilder()
          .startTableScan()
          .outputType(
              ROW({"requestId", "userId", "path"},
                  {VARCHAR(), VARCHAR(), VARCHAR()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_1"))
          .assignments({
              {"requestId",
               std::make_shared<ClpColumnHandle>(
                   "requestId", "requestId", VARCHAR())},
              {"userId",
               std::make_shared<ClpColumnHandle>(
                   "userId", "userId", VARCHAR())},
              {"path",
               std::make_shared<ClpColumnHandle>("path", "path", VARCHAR())},
          })
          .endTableScan()
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_1.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// requestId
       makeFlatVector<StringView>({"req-106"}),
       // userId
       makeNullableFlatVector<StringView>({std::nullopt}),
       // path
       makeFlatVector<StringView>({"/auth/login"})});
  test::assertEqualVectors(expected, output);

  auto irOutput = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_1_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery)});
  test::assertEqualVectors(expected, irOutput);
}

TEST_F(ClpConnectorTest, test1JsonString) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder()
          .startTableScan()
          .outputType(
              ROW({"requestId", "__json_string", "method"},
                  {VARCHAR(), VARCHAR(), VARCHAR()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_1"))
          .assignments({
              {"requestId",
               std::make_shared<ClpColumnHandle>(
                   "requestId", "requestId", VARCHAR())},
              {"__json_string",
               std::make_shared<ClpColumnHandle>(
                   "__json_string", "__json_string", VARCHAR())},
              {"method",
               std::make_shared<ClpColumnHandle>(
                   "method", "method", VARCHAR())},
          })
          .endTableScan()
          .filter("method = 'GET'")
          .planNode();

  const auto methodVector = makeFlatVector<StringView>({
      "GET",
      "GET",
      "GET",
      "GET",
      "GET",
  });
  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_1.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// requestId
       makeFlatVector<StringView>(
           {"req-100", "req-105", "req-107", "req-109", "req-102"}),
       // __json_string
       makeFlatVector<StringView>({
           R"({"timestamp":"2025-04-30T08:45:00Z","requestId":"req-100","userId":"user201","method":"GET","path":"/api/users/1","responseTimeMs":25,"status":200})",
           R"({"timestamp":"2025-04-30T08:45:25Z","requestId":"req-105","userId":"user204","method":"GET","path":"/api/dashboard","responseTimeMs":155,"status":200})",
           R"({"timestamp":"2025-04-30T08:45:35Z","requestId":"req-107","userId":"user202","method":"GET","path":"/api/users/2/details","responseTimeMs":41,"status":200})",
           R"({"timestamp":"2025-04-30T08:45:45Z","requestId":"req-109","userId":"user203","method":"GET","path":"/api/products?category=books","responseTimeMs":88,"status":200})",
           R"({"timestamp":"2025-04-30T08:45:10Z","requestId":"req-102","method":"GET","path":"/public/products","responseTimeMs":18,"status":200,"userId":null})",
       }),
       // method
       methodVector});
  test::assertEqualVectors(expected, output);

  auto irOutput = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_1_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery)});
  auto irExpected = makeRowVector(
      {// requestId
       makeFlatVector<StringView>(
           {"req-100", "req-102", "req-105", "req-107", "req-109"}),
       // __json_string
       makeFlatVector<StringView>({
           R"({"method":"GET","path":"/api/users/1","requestId":"req-100","responseTimeMs":25,"status":200,"timestamp":"2025-04-30T08:45:00Z","userId":"user201"})",
           R"({"method":"GET","path":"/public/products","requestId":"req-102","responseTimeMs":18,"status":200,"timestamp":"2025-04-30T08:45:10Z","userId":null})",
           R"({"method":"GET","path":"/api/dashboard","requestId":"req-105","responseTimeMs":155,"status":200,"timestamp":"2025-04-30T08:45:25Z","userId":"user204"})",
           R"({"method":"GET","path":"/api/users/2/details","requestId":"req-107","responseTimeMs":41,"status":200,"timestamp":"2025-04-30T08:45:35Z","userId":"user202"})",
           R"({"method":"GET","path":"/api/products?category=books","requestId":"req-109","responseTimeMs":88,"status":200,"timestamp":"2025-04-30T08:45:45Z","userId":"user203"})",
       }),
       // method
       methodVector});
  test::assertEqualVectors(irExpected, irOutput);
}

TEST_F(ClpConnectorTest, test2NoPushdown) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(
              ROW({"timestamp", "event"},
                  {TIMESTAMP(),
                   ROW({"type", "subtype", "severity"},
                       {VARCHAR(), VARCHAR(), VARCHAR()})}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_2"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"event",
                std::make_shared<ClpColumnHandle>(
                    "event",
                    "event",
                    ROW({"type", "subtype", "severity"},
                        {VARCHAR(), VARCHAR(), VARCHAR()}))}})
          .endTableScan()
          .filter(
              "event.severity IN ('WARNING', 'ERROR') AND "
              "((event.type = 'network' AND event.subtype = 'connection') OR "
              "(event.type = 'storage' AND event.subtype LIKE 'disk_usage%'))")
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_2.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds)}),
       // event
       makeRowVector({
           // event.type
           makeFlatVector<StringView>({"storage"}),
           // event.subtype
           makeFlatVector<StringView>({"disk_usage"}),
           // event.severity
           makeFlatVector<StringView>({"WARNING"}),
       })});
  test::assertEqualVectors(expected, output);

  auto irOutput = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_2_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery)});
  test::assertEqualVectors(expected, irOutput);
}

TEST_F(ClpConnectorTest, test2Pushdown) {
  auto kqlQuery = std::make_shared<std::string>(
      "(event.severity: \"WARNING\" OR event.severity: \"ERROR\") AND "
      "((event.type: \"network\" AND event.subtype: \"connection\") OR "
      "(event.type: \"storage\" AND event.subtype: \"disk*\"))");
  auto plan =
      PlanBuilder()
          .startTableScan()
          .outputType(
              ROW({"timestamp", "event"},
                  {TIMESTAMP(),
                   ROW({"type", "subtype", "severity"},
                       {VARCHAR(), VARCHAR(), VARCHAR()})}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_2"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"event",
                std::make_shared<ClpColumnHandle>(
                    "event",
                    "event",
                    ROW({"type", "subtype", "severity"},
                        {VARCHAR(), VARCHAR(), VARCHAR()}))}})
          .endTableScan()
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_2.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds)}),
       // event
       makeRowVector({
           // event.type
           makeFlatVector<StringView>({"storage"}),
           // event.subtype
           makeFlatVector<StringView>({"disk_usage"}),
           // event.severity
           makeFlatVector<StringView>({"WARNING"}),
       })});
  test::assertEqualVectors(expected, output);

  auto irOutput = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_2_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery)});
  test::assertEqualVectors(expected, irOutput);
}

TEST_F(ClpConnectorTest, test2Hybrid) {
  auto kqlQuery = std::make_shared<std::string>(
      "((event.type: \"network\" AND event.subtype: \"connection\") OR "
      "(event.type: \"storage\" AND event.subtype: \"disk*\"))");
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(
              ROW({"timestamp", "event"},
                  {TIMESTAMP(),
                   ROW({"type", "subtype", "severity", "tags"},
                       {VARCHAR(), VARCHAR(), VARCHAR(), ARRAY(VARCHAR())})}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_2"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"event",
                std::make_shared<ClpColumnHandle>(
                    "event",
                    "event",
                    ROW({"type", "subtype", "severity", "tags"},
                        {VARCHAR(), VARCHAR(), VARCHAR(), ARRAY(VARCHAR())}))}})
          .endTableScan()
          .filter("upper(event.severity) IN ('WARNING', 'ERROR')")
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_2.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds)}),
       // event
       makeRowVector(
           {// event.type
            makeFlatVector<StringView>({"storage"}),
            // event.subtype
            makeFlatVector<StringView>({"disk_usage"}),
            // event.severity
            makeFlatVector<StringView>({"WARNING"}),
            // event.tags
            makeArrayVector<StringView>(
                {{"\"filesystem\"", "\"monitoring\""}})})

      });
  test::assertEqualVectors(expected, output);

  auto irOutput = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_2_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery)});
  test::assertEqualVectors(expected, irOutput);
}

TEST_F(ClpConnectorTest, test2JsonString) {
  auto kqlQuery = std::make_shared<std::string>(
      "(event.severity: \"WARNING\" OR event.severity: \"ERROR\") AND "
      "((event.type: \"network\" AND event.subtype: \"connection\") OR "
      "(event.type: \"storage\" AND event.subtype: \"disk*\"))");
  auto plan =
      PlanBuilder()
          .startTableScan()
          .outputType(
              ROW({"timestamp", "__json_string"}, {TIMESTAMP(), VARCHAR()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_2"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"__json_string",
                std::make_shared<ClpColumnHandle>(
                    "__json_string", "__json_string", VARCHAR())}})
          .endTableScan()
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_2.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds)}),
       // __json_string
       makeFlatVector<StringView>(
           {R"({"timestamp":"2025-04-30T08:50:05Z","event":{"type":"storage","subtype":"disk_usage","severity":"WARNING","tags":["filesystem", "monitoring"],"details":{"mount":"/var/log","usage":{"percent":92}}}})"})});
  test::assertEqualVectors(expected, output);

  auto irOutput = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_2_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery)});
  auto irExpected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds)}),
       // __json_string
       makeFlatVector<StringView>(
           {R"({"event":{"details":{"mount":"/var/log","usage":{"percent":92}},"severity":"WARNING","subtype":"disk_usage","tags":["filesystem","monitoring"],"type":"storage"},"timestamp":1746003005})"})});

  test::assertEqualVectors(irExpected, irOutput);
}

TEST_F(ClpConnectorTest, test3TimestampMarshalling) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp"}, {TIMESTAMP()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_3"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())}})
          .endTableScan()
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_3.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector({
      // timestamp
      makeFlatVector<Timestamp>(
          {Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds),
           Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds),
           Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds),
           Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds)}),
  });
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test4IrTimestampNoPushdown) {
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp"}, {TIMESTAMP()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_4"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())}})
          .endTableScan()
          .filter("\"timestamp\" < timestamp '2025-08-24 02:36:45'")
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_4_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery)});
  auto expected = makeRowVector({
      // timestamp
      makeFlatVector<Timestamp>(
          {Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds)}),
  });
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test4IrTimestampPushdown) {
  // Only the second event meet the condition, the first event is a date string
  // which is not supported yet so the value will be NULL.
  // This test can not use the `timestamp()` literal, since the integer
  // timestamps are in microsecond precision, and we currently assume all IR
  // timestamps are millisecond precision when comparing against timestamp
  // literals.
  const std::shared_ptr<std::string> kqlQuery =
      std::make_shared<std::string>("(timestamp < 1756003005000000)");
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp"}, {TIMESTAMP()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_4"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())}})
          .endTableScan()
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_4_ir.clp.zst"),
          ClpConnectorSplit::SplitType::kIr,
          kqlQuery)});
  auto expected = makeRowVector({
      // timestamp
      makeFlatVector<Timestamp>(
          {Timestamp(kTestTimestampSeconds, kTestTimestampNanoseconds)}),
  });
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test5FloatTimestampNoPushdown) {
  // Test filtering rows with a timestamp parsed from a date string and floats
  // in various formats.
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp", "floatValue"}, {TIMESTAMP(), DOUBLE()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_5"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"floatValue",
                std::make_shared<ClpColumnHandle>(
                    "floatValue", "floatValue", DOUBLE())}})
          .endTableScan()
          .filter(
              "\"timestamp\" < timestamp '2025-04-30 08:51:10' AND \"timestamp\" >= timestamp '2025-04-30 08:50:05.124'")
          .orderBy({"\"timestamp\" ASC"}, false)
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_5.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(1746003005, 124000000),
            Timestamp(1746003005, 124100000),
            Timestamp(1746003005, 125000000),
            Timestamp(1746003005, 126000000),
            Timestamp(1746003005, 127000000),
            Timestamp(1746003060, 0),
            Timestamp(1746003065, 0)}),
       makeFlatVector<double>(
           {1.2345678912345E9,
            1E16,
            1.234567891234567E9,
            1.234567891234567E9,
            -1.234567891234567E-9,
            1234567891.234567,
            -1234567891.234567})});
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test5FloatTimestampPushdown) {
  // Test filtering rows with a timestamp parsed from a date string and floats
  // in various formats.
  const std::shared_ptr<std::string> kqlQuery = std::make_shared<std::string>(
      R"(timestamp < timestamp("1746003070000", "\L") and timestamp >= timestamp("1746003005124", "\L"))");
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp", "floatValue"}, {TIMESTAMP(), DOUBLE()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_5"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"floatValue",
                std::make_shared<ClpColumnHandle>(
                    "floatValue", "floatValue", DOUBLE())}})
          .endTableScan()
          .orderBy({"\"timestamp\" ASC"}, false)
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_5.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(1746003005, 124000000),
            Timestamp(1746003005, 124100000),
            Timestamp(1746003005, 125000000),
            Timestamp(1746003005, 126000000),
            Timestamp(1746003005, 127000000),
            Timestamp(1746003060, 0),
            Timestamp(1746003065, 0)}),
       makeFlatVector<double>(
           {1.2345678912345E9,
            1E16,
            1.234567891234567E9,
            1.234567891234567E9,
            -1.234567891234567E-9,
            1234567891.234567,
            -1234567891.234567})});
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test5NewTimestampFormatFloatTimestampPushdown) {
  // Test filtering rows with a timestamp parsed from a date string and floats
  // in various formats.
  const std::shared_ptr<std::string> kqlQuery = std::make_shared<std::string>(
      R"(timestamp < timestamp("1746003070000", "\L") and timestamp >= timestamp("1746003005124", "\L"))");
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp", "floatValue"}, {TIMESTAMP(), DOUBLE()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_5"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"floatValue",
                std::make_shared<ClpColumnHandle>(
                    "floatValue", "floatValue", DOUBLE())}})
          .endTableScan()
          .orderBy({"\"timestamp\" ASC"}, false)
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_5.v0.5.0.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(1746003005, 124000000),
            Timestamp(1746003005, 124100000),
            Timestamp(1746003005, 125000000),
            Timestamp(1746003005, 126000000),
            Timestamp(1746003005, 127000001),
            Timestamp(1746003060, 0),
            Timestamp(1746003065, 0)}),
       makeFlatVector<double>(
           {1.2345678912345E9,
            1E16,
            1.234567891234567E9,
            1.234567891234567E9,
            -1.234567891234567E-9,
            1234567891.234567,
            -1234567891.234567})});
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test5FormattedFloatNoPushdown) {
  // Test floats of only FormattedFloat type
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp", "floatValue"}, {TIMESTAMP(), DOUBLE()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_5"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"floatValue",
                std::make_shared<ClpColumnHandle>(
                    "floatValue", "floatValue", DOUBLE())}})
          .endTableScan()
          .filter(
              "\"floatValue\" = 0.0 OR \"floatValue\" = 1.2345678912345E-29")
          .orderBy({"\"timestamp\" ASC"}, false)
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_5.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(1746003005, 123457000),
            Timestamp(1746003115, 0),
            Timestamp(1746003120, 0),
            Timestamp(1746003125, 0),
            Timestamp(1746003130, 0),
            Timestamp(1746003135, 0),
            Timestamp(1746003140, 0),
            Timestamp(1746003145, 0),
            Timestamp(1746003185, 0),
            Timestamp(1746003190, 0)}),
       makeFlatVector<double>(
           {1.2345678912345E-29,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0})});
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test5FormattedFloatPushdown) {
  // Test floats of only FormattedFloat type
  const std::shared_ptr<std::string> kqlQuery = std::make_shared<std::string>(
      "(floatValue: 0.0 or floatValue: 1.2345678912345E-29)");
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp", "floatValue"}, {TIMESTAMP(), DOUBLE()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_5"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"floatValue",
                std::make_shared<ClpColumnHandle>(
                    "floatValue", "floatValue", DOUBLE())}})
          .endTableScan()
          .orderBy({"\"timestamp\" ASC"}, false)
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_5.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(1746003005, 123457000),
            Timestamp(1746003115, 0),
            Timestamp(1746003120, 0),
            Timestamp(1746003125, 0),
            Timestamp(1746003130, 0),
            Timestamp(1746003135, 0),
            Timestamp(1746003140, 0),
            Timestamp(1746003145, 0),
            Timestamp(1746003185, 0),
            Timestamp(1746003190, 0)}),
       makeFlatVector<double>(
           {1.2345678912345E-29,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0,
            0.0})});
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test5DictionaryFloatNoPushdown) {
  // Test floats of only DictionaryFloat type
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp", "floatValue"}, {TIMESTAMP(), DOUBLE()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_5"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"floatValue",
                std::make_shared<ClpColumnHandle>(
                    "floatValue", "floatValue", DOUBLE())}})
          .endTableScan()
          .filter("\"floatValue\" > 1.999999  AND \"floatValue\" < 2.000001")
          .orderBy({"\"timestamp\" ASC"}, false)
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_5.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(1746003195, 0), Timestamp(1746003200, 0)}),
       makeFlatVector<double>({2, 2})});
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test5DictionaryFloatPushdown) {
  // Test floats of only DictionaryFloat type
  const std::shared_ptr<std::string> kqlQuery = std::make_shared<std::string>(
      "(floatValue > 1.999999 and floatValue < 2.000001)");
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp", "floatValue"}, {TIMESTAMP(), DOUBLE()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_5"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"floatValue",
                std::make_shared<ClpColumnHandle>(
                    "floatValue", "floatValue", DOUBLE())}})
          .endTableScan()
          .orderBy({"\"timestamp\" ASC"}, false)
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_5.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>(
           {Timestamp(1746003195, 0), Timestamp(1746003200, 0)}),
       makeFlatVector<double>({2, 2})});
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test5HybridNoPushdown) {
  // Test floats of both FormattedFloat and DictionaryFloat types
  const std::shared_ptr<std::string> kqlQuery = nullptr;
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp", "floatValue"}, {TIMESTAMP(), DOUBLE()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_5"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"floatValue",
                std::make_shared<ClpColumnHandle>(
                    "floatValue", "floatValue", DOUBLE())}})
          .endTableScan()
          .filter("\"floatValue\" = 1.0")
          .orderBy({"\"timestamp\" ASC"}, false)
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_5.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>({
           Timestamp(1746003105, 0),
           Timestamp(1746003110, 0),
           Timestamp(1746003150, 0),
           Timestamp(1746003155, 0),
           Timestamp(1746003160, 0),
           Timestamp(1746003205, 0),
       }),
       makeFlatVector<double>({1, 1, 1, 1, 1, 1})});
  test::assertEqualVectors(expected, output);
}

TEST_F(ClpConnectorTest, test5HybridPushdown) {
  // Test floats of both FormattedFloat and DictionaryFloat types
  const std::shared_ptr<std::string> kqlQuery =
      std::make_shared<std::string>("(floatValue: 1.0)");
  auto plan =
      PlanBuilder(pool_.get())
          .startTableScan()
          .outputType(ROW({"timestamp", "floatValue"}, {TIMESTAMP(), DOUBLE()}))
          .tableHandle(
              std::make_shared<ClpTableHandle>(kClpConnectorId, "test_5"))
          .assignments(
              {{"timestamp",
                std::make_shared<ClpColumnHandle>(
                    "timestamp", "timestamp", TIMESTAMP())},
               {"floatValue",
                std::make_shared<ClpColumnHandle>(
                    "floatValue", "floatValue", DOUBLE())}})
          .endTableScan()
          .orderBy({"\"timestamp\" ASC"}, false)
          .planNode();

  auto output = getResults(
      plan,
      {makeClpSplit(
          getExampleFilePath("test_5.clps"),
          ClpConnectorSplit::SplitType::kArchive,
          kqlQuery)});
  auto expected = makeRowVector(
      {// timestamp
       makeFlatVector<Timestamp>({
           Timestamp(1746003105, 0),
           Timestamp(1746003110, 0),
           Timestamp(1746003150, 0),
           Timestamp(1746003155, 0),
           Timestamp(1746003160, 0),
           Timestamp(1746003205, 0),
       }),
       makeFlatVector<double>({1, 1, 1, 1, 1, 1})});
  test::assertEqualVectors(expected, output);
}

} // namespace

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  folly::Init init{&argc, &argv, false};
  return RUN_ALL_TESTS();
}
