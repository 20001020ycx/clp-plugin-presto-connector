add_subdirectory(archive)
add_subdirectory(ir)

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
        PRIVATE
        clp_s::search::kql
        clp_s::search::ast
)
