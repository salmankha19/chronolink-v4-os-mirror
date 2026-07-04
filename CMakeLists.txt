cmake_minimum_required(VERSION 3.5)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(chronolink_v4_os)

# Make the project's public headers available to all components
include_directories(${CMAKE_SOURCE_DIR}/include)