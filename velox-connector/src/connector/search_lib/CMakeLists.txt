add_subdirectory(archive)
add_subdirectory(ir)

# The scan's timers and counters. Free of Velox and CLP, so `tests/` builds the same source.
add_library(clp_connector_scan_stats STATIC
        ClpScanStats.cpp
)

target_include_directories(clp_connector_scan_stats
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

add_library(clp_connector_search STATIC
        BaseClpCursor.cpp
        ClpS3AuthProviderBase.cpp
        ClpPackageS3AuthProvider.cpp
)

target_include_directories(clp_connector_search
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
        ${PRESTO_ROOT}
        ${PRESTO_ROOT}/velox
)

target_link_libraries(clp_connector_search
        PUBLIC
        clp_connector_search_archive
        clp_connector_search_ir
        clp_connector_scan_stats
        PRIVATE
        clp_s::search::kql
        clp_s::search::ast
)
