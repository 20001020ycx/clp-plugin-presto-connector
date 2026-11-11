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

// Free of Velox, so `tests/` can build it outside the build-env image. See tests/README.md.

#pragma once

#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>

#ifdef __GLIBC__
#include <dlfcn.h>
#include <link.h>
#endif

namespace facebook::presto {

/// Adds one virtual function to `Base`. The compiler places the new function
/// right after Base's own entries in the vtable, so its offset there equals the
/// number of entries that `Base` has. No object of it is ever created.
template <typename Base>
struct ClpVtableEntryCountProbe : Base {
  virtual void clpVtableEntryCountProbe() {}
};

/// Counts the vtable entries that the compiler of the calling code gives a
/// class, one for each of its virtual functions and two for a virtual
/// destructor.
///
/// @tparam Base A polymorphic class that isn't final and that, like all of its
/// bases, has at most one polymorphic base and no virtual base.
/// @return The number of entries.
template <typename Base>
[[nodiscard]] size_t numCompiledVtableEntries() {
  static_assert(std::is_polymorphic_v<Base>);
  // In the Itanium C++ ABI, a pointer to a member function is the pair
  // {ptr, adj}. For a virtual function, ptr holds the function's offset in the
  // vtable, plus 1 on the architectures that mark a virtual function there.
  struct {
    std::ptrdiff_t ptr;
    std::ptrdiff_t adj;
  } representation;
  void (ClpVtableEntryCountProbe<Base>::* const probe)(){
      &ClpVtableEntryCountProbe<Base>::clpVtableEntryCountProbe};
  static_assert(sizeof(representation) == sizeof(probe));
  std::memcpy(&representation, &probe, sizeof(representation));
  // The division drops the mark.
  return static_cast<size_t>(representation.ptr) / sizeof(void*);
}

/// Counts the entries of the vtable that the running program exports for a
/// class, i.e., of the vtable that the program was compiled with. The lookup
/// covers the program, the libraries loaded with it, and the ones loaded later
/// with `RTLD_GLOBAL`, so a library loaded without `RTLD_GLOBAL` doesn't find
/// its own copy of the vtable.
///
/// @param type A class under the restrictions of `numCompiledVtableEntries`.
/// @return The number of entries.
/// @return std::nullopt if the program exports no vtable for the class, or if
/// the C library isn't glibc, whose `dladdr1` the lookup needs.
[[nodiscard]] inline std::optional<size_t> numExportedVtableEntries(
    [[maybe_unused]] const std::type_info& type) {
#ifdef __GLIBC__
  auto* const program{dlopen(nullptr, RTLD_NOW)};
  if (nullptr == program) {
    return std::nullopt;
  }
  auto const vtableSymbol{std::string{"_ZTV"} + type.name()};
  auto* const vtable{dlsym(program, vtableSymbol.c_str())};
  dlclose(program);
  if (nullptr == vtable) {
    return std::nullopt;
  }
  Dl_info info{};
  void* symbol{nullptr};
  if (0 == dladdr1(vtable, &info, &symbol, RTLD_DL_SYMENT) ||
      nullptr == symbol) {
    return std::nullopt;
  }
  // The entries follow two words: the offset to the top of the object and the
  // pointer to the type information.
  constexpr size_t kNumHeaderWords{2};
  auto const numWords{
      static_cast<const ElfW(Sym)*>(symbol)->st_size / sizeof(void*)};
  if (numWords < kNumHeaderWords) {
    return std::nullopt;
  }
  return numWords - kNumHeaderWords;
#else
  return std::nullopt;
#endif
}

} // namespace facebook::presto
