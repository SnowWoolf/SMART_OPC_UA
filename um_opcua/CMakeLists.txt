cmake_minimum_required(VERSION 3.13)
project(um_opcua C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(open62541 REQUIRED)
find_package(CURL REQUIRED)

find_path(CJSON_INCLUDE_DIR
    NAMES cJSON.h
    PATHS
        /usr/include
        /usr/include/cjson
)

find_library(CJSON_LIBRARY
    NAMES cjson libcjson
    PATHS
        /usr/lib
        /usr/lib/arm-linux-gnueabihf
        /usr/lib/aarch64-linux-gnu
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
)

if(NOT CJSON_INCLUDE_DIR)
    message(FATAL_ERROR "cJSON header not found")
endif()

if(NOT CJSON_LIBRARY)
    message(FATAL_ERROR "cJSON library not found")
endif()

add_executable(um_opcua
    src/main.c
    src/api_client.c
    src/tag_config.c
    src/opc_nodes.c
    src/opc_history.c
)

target_include_directories(um_opcua PRIVATE
    src
    ${CJSON_INCLUDE_DIR}
)

target_link_libraries(um_opcua PRIVATE
    open62541::open62541
    CURL::libcurl
    ${CJSON_LIBRARY}
)