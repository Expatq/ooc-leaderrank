cmake_minimum_required(VERSION 3.22)
project(ooc-leaderrank LANGUAGES CXX)

include(cmake/compile.cmake)
include(cmake/functions.cmake)

enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
