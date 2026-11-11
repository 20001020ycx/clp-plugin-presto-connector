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

#include "ClpVtableLayout.h"

#include <optional>
#include <typeinfo>

#include <gtest/gtest.h>

namespace facebook::presto {

// These classes have external linkage and define their virtual functions out
// of line, so that the test binary exports their vtables.

class OneFunction {
 public:
  virtual void first();
};

void OneFunction::first() {}

class WithVirtualDestructor {
 public:
  virtual ~WithVirtualDestructor();
  virtual void first();
  virtual void second();
};

WithVirtualDestructor::~WithVirtualDestructor() = default;
void WithVirtualDestructor::first() {}
void WithVirtualDestructor::second() {}

class Derived : public WithVirtualDestructor {
 public:
  ~Derived() override;
  void first() override;
  virtual void third();
};

Derived::~Derived() = default;
void Derived::first() {}
void Derived::third() {}

class Interface {
 public:
  virtual ~Interface();
  virtual void first() = 0;
  virtual void second() = 0;
};

Interface::~Interface() = default;

namespace {

// Has internal linkage, so the test binary doesn't export its vtable.
class NotExported {
 public:
  virtual void first() {}
};

TEST(ClpVtableLayoutTest, countsEachVirtualFunction) {
  EXPECT_EQ(numCompiledVtableEntries<OneFunction>(), 1U);
}

TEST(ClpVtableLayoutTest, countsVirtualDestructorTwice) {
  EXPECT_EQ(numCompiledVtableEntries<WithVirtualDestructor>(), 4U);
}

TEST(ClpVtableLayoutTest, countsInheritedAndAddedFunctions) {
  EXPECT_EQ(numCompiledVtableEntries<Derived>(), 5U);
}

TEST(ClpVtableLayoutTest, countsPureVirtualFunctions) {
  EXPECT_EQ(numCompiledVtableEntries<Interface>(), 4U);
}

#ifdef __GLIBC__
// The plugin compares these two counts to tell whether the worker was built
// with the same interfaces, so they must agree within one binary.
TEST(ClpVtableLayoutTest, exportedVtablesHaveTheCompiledNumberOfEntries) {
  EXPECT_EQ(
      numExportedVtableEntries(typeid(OneFunction)),
      numCompiledVtableEntries<OneFunction>());
  EXPECT_EQ(
      numExportedVtableEntries(typeid(WithVirtualDestructor)),
      numCompiledVtableEntries<WithVirtualDestructor>());
  EXPECT_EQ(
      numExportedVtableEntries(typeid(Derived)),
      numCompiledVtableEntries<Derived>());
  EXPECT_EQ(
      numExportedVtableEntries(typeid(Interface)),
      numCompiledVtableEntries<Interface>());
}
#endif

TEST(ClpVtableLayoutTest, findsNoVtableThatTheProgramDoesNotExport) {
  EXPECT_EQ(numExportedVtableEntries(typeid(NotExported)), std::nullopt);
}

} // namespace
} // namespace facebook::presto
