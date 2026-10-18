cmake_minimum_required(VERSION 4.3)
project(lorikeet)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(external/keet)

add_executable(lorikeet main.cpp)

target_link_libraries(lorikeet PRIVATE keet)