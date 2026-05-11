add_subdirectory(search_lib)

add_library(presto_clp_connector STATIC
        ClpConfig.cpp
        ClpConnector.cpp
        ClpDataSource.cpp
        ClpTableHandle.cpp
)

target_include_directories(presto_clp_connector
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${PRESTO_ROOT}
        ${PRESTO_ROOT}/velox
)

target_link_libraries(presto_clp_connector
        PUBLIC
        clp_search
        PRIVATE
        clp_s::search
)
