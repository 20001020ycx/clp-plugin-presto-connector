add_library(presto_clp_protocol STATIC
        ClpConnectorProtocol.cpp
)

target_include_directories(presto_clp_protocol
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${PRESTO_ROOT}
        ${PRESTO_ROOT}/velox
)
