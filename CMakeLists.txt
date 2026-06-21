cmake_minimum_required(VERSION 3.16)

# This line MUST exist before including the project engine
list(APPEND EXTRA_COMPONENT_DIRS "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(chronolink_v4_os)