add_subdirectory(archive)
add_subdirectory(ir)

# Free of Velox, so that the unit tests can link it without the Presto worker: none of its include
# directories reach Velox.
add_library(clp_connector_s3_auth STATIC
        AwsDefaultS3CredentialsProvider.cpp
        S3NetworkAuth.cpp
)

target_include_directories(clp_connector_s3_auth
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

# The scan's timers and counters. Free of Velox and CLP, so `tests/` builds the same source.
add_library(clp_connector_scan_stats STATIC
        ClpScanStats.cpp
)

target_include_directories(clp_connector_scan_stats
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(clp_connector_s3_auth
        PUBLIC
        clp_s::io
        PRIVATE
        aws-cpp-sdk-core
        clp_s::clp_dependencies
)

add_library(clp_connector_search STATIC
        BaseClpCursor.cpp
        ClpDefaultS3AuthProvider.cpp
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
        clp_connector_s3_auth
        clp_connector_search_archive
        clp_connector_search_ir
        clp_connector_scan_stats
        PRIVATE
        clp_s::search::kql
        clp_s::search::ast
)

if(CLP_PLUGIN_BUILD_TESTING)
    add_subdirectory(tests)
endif()
