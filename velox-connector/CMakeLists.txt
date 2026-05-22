# NOTE: 3.28.3 is the highest CMake version required by this plugin's dependencies.
cmake_minimum_required(VERSION 3.28.3)

project(libclp-plugin-velox-connector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable -fPIC for all targets so STATIC libraries can be linked into a shared library.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Match the Presto worker's AVX2 flags to satisfy Folly's F14LinkCheck<Mode> symbol.
#
# NOTE: Including Folly's F14 headers emits a call to F14LinkCheck<Mode>, where Mode is determined
# at compile time by the CPU flags. This plugin includes the Presto worker's headers, which pull in
# Folly's F14 headers transitively. The Presto worker is built with AVX2 (Mode 2); if this plugin is
# built without it, it emits a call to F14LinkCheck<1>, which the Presto worker does not provide,
# causing dlopen to fail with an unresolved symbol error.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")

# ==============================================================================
# Presto/Velox framework headers
# ==============================================================================
include(FetchContent)

message(STATUS "Downloading presto for headers (commit: 8a20b604c8c6348aab671bcd723ea4482c9e3603)")
FetchContent_Declare(
    presto_native_execution
    GIT_REPOSITORY    https://github.com/y-scope/presto.git
    GIT_TAG           8a20b604c8c6348aab671bcd723ea4482c9e3603
    GIT_SUBMODULES    "presto-native-execution/velox"
    GIT_SUBMODULES_RECURSE OFF
)
FetchContent_MakeAvailable(presto_native_execution)
set(PRESTO_ROOT ${presto_native_execution_SOURCE_DIR}/presto-native-execution)

# ==============================================================================
# CLP
# ==============================================================================

# CLP options — only build what the plugin needs.
set(CLP_BUILD_CLP_REGEX_UTILS OFF CACHE BOOL "Build CLP regex utils")
set(CLP_BUILD_CLP_S_JSONCONSTRUCTOR OFF CACHE BOOL "Build CLP-S JSON constructor")
set(CLP_BUILD_CLP_S_REDUCER_DEPENDENCIES OFF CACHE BOOL "Build CLP-S reducer dependencies")
set(CLP_BUILD_CLP_S_SEARCH_SQL OFF CACHE BOOL "Build CLP-S search SQL")
set(CLP_BUILD_EXECUTABLES OFF CACHE BOOL "Build CLP executables")
set(CLP_BUILD_TESTING OFF CACHE BOOL "Build CLP tests")

message(STATUS "Downloading CLP (commit: a91e5f71f0715d7d6b3ea7c177e1b39b3e6a24a6)")
FetchContent_Declare(clp
  GIT_REPOSITORY https://github.com/y-scope/clp.git
  GIT_TAG        a91e5f71f0715d7d6b3ea7c177e1b39b3e6a24a6
  SOURCE_SUBDIR components/core
)
FetchContent_MakeAvailable(clp)

# ==============================================================================
# Plugin target
# ==============================================================================

# Plugin source tree
add_subdirectory(src)

# Plugin target
add_library(clp-plugin-velox-connector SHARED
        src/ClpPluginEntry.cpp
        src/ClpPrestoToVeloxConnector.cpp
)

target_include_directories(clp-plugin-velox-connector
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${PRESTO_ROOT}
        ${PRESTO_ROOT}/velox
)

target_link_libraries(clp-plugin-velox-connector
        PRIVATE
        presto_clp_protocol
        presto_clp_connector
)

# Hide all symbols by default to prevent ODR clashes with the Presto worker.
#
# NOTE: The plugin and the Presto worker share overlapping dependencies (e.g., fmt) that may differ
# in version. Without visibility control, the dynamic linker could resolve the plugin's calls to
# the Presto worker's copy of those libraries at dlopen time, causing undefined behavior. The
# following ensure the plugin uses its own copies of each dependency:
#
# - Static linking embeds dependency code into the .so (resolved at link time)
# - CXX_VISIBILITY_PRESET hidden hides those symbols from the dynamic table
# - VISIBILITY_INLINES_HIDDEN hides inline function symbols too
set_target_properties(clp-plugin-velox-connector
        PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)

target_link_options(clp-plugin-velox-connector
        PRIVATE
        -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/plugin.map
)
