cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS version.txt)
if(NOT PROJECT_VER MATCHES "^[1-9][0-9]*$" OR PROJECT_VER GREATER 4294967295)
    message(FATAL_ERROR "version.txt must contain an integer from 1 to 4294967295")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_01_otademo)
