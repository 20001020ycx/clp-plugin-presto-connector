add_library(clp_search_archive STATIC
        ClpArchiveCursor.cpp
        ClpArchiveVectorLoader.cpp
        ClpArchiveJsonStringVectorLoader.cpp
        ClpQueryRunner.cpp
)

target_include_directories(clp_search_archive
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../..
        ${PRESTO_ROOT}
        ${PRESTO_ROOT}/velox
)

target_link_libraries(clp_search_archive
        PRIVATE
        clp_s::archive_reader
        clp_s::clp_dependencies
        clp_s::io
        clp_s::search
        clp_s::search::kql
)
