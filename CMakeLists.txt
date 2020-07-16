cmake_minimum_required(VERSION 3.10 FATAL_ERROR)


# *****************************************************************************
# Project Canary
# *****************************************************************************
project(canary VERSION 1.0.0 LANGUAGES CXX)


# *****************************************************************************
# Configure CanaryLib variable
# *****************************************************************************
set(CANARY_LIB_PATH ${CMAKE_SOURCE_DIR}/canary-lib)


# *****************************************************************************
# Configure 3rd party variables
# *****************************************************************************
set(CANARY_3RD_PARTY_PATH             ${CMAKE_SOURCE_DIR}/3rd)
set(CANARY_EXTERNAL_PATH              ${CMAKE_SOURCE_DIR}/external)
set(CANARY_EXTERNAL_OUTPUT_DIRECTORY  ${CMAKE_SOURCE_DIR}/build/external)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)


# *****************************************************************************
# Include cmake tools
# *****************************************************************************
include(MessageColors)
include(LoggingHelper)
include(ToolGitSubmodule)


# *****************************************************************************
# Add 3rd party libraries
# *****************************************************************************
add_subdirectory(3rd)
add_subdirectory(canary-lib)


# *****************************************************************************
# Add Source Code
# *****************************************************************************
add_subdirectory(src)
