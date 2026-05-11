add_library(clp_search_ir STATIC
        ClpIrCursor.cpp
        ClpIrVectorLoader.cpp
        ClpIrJsonStringVectorLoader.cpp
)

target_include_directories(clp_search_ir
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../..
        ${PRESTO_ROOT}
        ${PRESTO_ROOT}/velox
)

target_link_libraries(clp_search_ir
        PRIVATE
        clp_s::archive_reader
        clp_s::clp_dependencies
        clp_s::io
        clp_s::search
        clp_s::search::ast
        clp_s::search::kql
)
