set(CMAKE_C_COMPILER clang)

cmake_minimum_required(VERSION 3.10)
project (OurSql)

set(CMAKE_C_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug)

include_directories(include)

file(GLOB SOURCES "*.c")
add_executable(oursql ${SOURCES})
