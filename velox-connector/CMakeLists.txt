# NOTE: 3.28.3 is the highest CMake version required by this plugin's dependencies.
cmake_minimum_required(VERSION 3.28.3)

project(libclp-plugin-velox-connector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable -fPIC for all targets so STATIC libraries can be linked into a shared library.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ==============================================================================
# Dependencies
# ==============================================================================

# Use the dependencies installed via `task velox-connector:deps:install-all`.
#
# NOTE: `all-deps.cmake` is auto-generated and sets `<Package>_ROOT` cache variables (e.g.
# `fmt_ROOT`) that point to the `task`-installed dependencies. CMake's `find_package()` searches
# `<Package>_ROOT` first, so the `task`-installed dependencies will be used instead of any system
# packages.
set(
    LIBCLP_PLUGIN_VELOX_CONNECTOR_DEPS_CMAKE_SETTINGS
    "${CMAKE_CURRENT_LIST_DIR}/../build/velox-connector/deps/cpp/cmake-settings/all-deps.cmake"
    CACHE
    STRING
    "Path to the CMake settings file describing this library's dependencies."
)

include("${LIBCLP_PLUGIN_VELOX_CONNECTOR_DEPS_CMAKE_SETTINGS}")

# ==============================================================================
# Presto/Velox framework headers
# ==============================================================================
include(FetchContent)

set(PRESTO_GIT_URL "" CACHE STRING "Git repository to fetch Presto from.")
set(PRESTO_GIT_TAG "" CACHE STRING "Presto commit that provides the Velox framework headers.")
if(PRESTO_GIT_URL STREQUAL "" OR PRESTO_GIT_TAG STREQUAL "")
    message(FATAL_ERROR
        "PRESTO_GIT_URL and PRESTO_GIT_TAG must be set. Build via"
        " `task velox-connector:build`, or set them when configuring CMake directly."
    )
endif()

message(STATUS "Downloading presto for headers (commit: ${PRESTO_GIT_TAG})")
FetchContent_Declare(
    presto_native_execution
    GIT_REPOSITORY    "${PRESTO_GIT_URL}"
    GIT_TAG           "${PRESTO_GIT_TAG}"
    GIT_SUBMODULES    "presto-native-execution/velox"
    GIT_SUBMODULES_RECURSE OFF
)
FetchContent_MakeAvailable(presto_native_execution)
set(PRESTO_ROOT ${presto_native_execution_SOURCE_DIR}/presto-native-execution)

# Compile with the same target-CPU flags as the Presto worker, which refuses to load a plugin
# built with different flags (see cmake/DeriveTargetCpuFlags.cmake).
include("${CMAKE_CURRENT_LIST_DIR}/cmake/DeriveTargetCpuFlags.cmake")
derive_target_cpu_flags(
    VELOX_TARGET_CPU_CXX_FLAGS
    # Velox's `get_cxx_flags` helper, from the FetchContent checkout pinned above.
    "${PRESTO_ROOT}/velox/scripts/setup-helper-functions.sh"
)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${VELOX_TARGET_CPU_CXX_FLAGS}")

# ==============================================================================
# CLP
# ==============================================================================

# CLP options - only build what the plugin needs.
set(CLP_BUILD_CLP_REGEX_UTILS OFF CACHE BOOL "Build CLP regex utils")
set(CLP_BUILD_CLP_S_ARCHIVEWRITER OFF CACHE BOOL "Build CLP-S archive writer")
# NOTE: CLP still finds LibArchive with this OFF, so the build image installs libarchive-devel, but
# the plugin doesn't link it.
set(CLP_BUILD_CLP_S_ENABLE_LIBARCHIVE OFF CACHE BOOL "Include libarchive support for clp-s")
set(CLP_BUILD_CLP_S_FILTER OFF CACHE BOOL "Build CLP-S filter")
set(CLP_BUILD_CLP_S_JSONCONSTRUCTOR OFF CACHE BOOL "Build CLP-S JSON constructor")
set(CLP_BUILD_CLP_S_REDUCER_DEPENDENCIES OFF CACHE BOOL "Build CLP-S reducer dependencies")
set(CLP_BUILD_CLP_S_SEARCH_SQL OFF CACHE BOOL "Build CLP-S search SQL")
set(CLP_BUILD_EXECUTABLES OFF CACHE BOOL "Build CLP executables")
set(CLP_BUILD_TESTING OFF CACHE BOOL "Build CLP tests")

message(STATUS "Downloading CLP (commit: d2d6beef02ab548c96c9a2115f54f9ae97bd0fb6)")
FetchContent_Declare(clp
  GIT_REPOSITORY https://github.com/y-scope/clp.git
  GIT_TAG        d2d6beef02ab548c96c9a2115f54f9ae97bd0fb6
  SOURCE_SUBDIR components/core
)
FetchContent_MakeAvailable(clp)

# ==============================================================================
# Plugin target
# ==============================================================================

# Compile-time include paths for the third-party headers transitively pulled in by the Velox
# headers. The libraries themselves are resolved at runtime by the Presto worker.
include_directories(
        ${absl_ROOT}/include
        ${Boost_ROOT}/include
        ${double-conversion_ROOT}/include
        ${fmt_ROOT}/include
        ${folly_ROOT}/include
        ${gflags_ROOT}/include
        ${glog_ROOT}/include
        ${re2_ROOT}
        ${xsimd_ROOT}/include
)

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
