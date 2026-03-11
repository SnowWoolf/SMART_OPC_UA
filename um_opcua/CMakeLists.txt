cmake_minimum_required(VERSION 3.13)
project(um_opcua C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(open62541 REQUIRED)
find_package(CURL REQUIRED)

add_executable(um_opcua
    src/main.c
)

target_link_libraries(um_opcua
    open62541::open62541
    CURL::libcurl
)