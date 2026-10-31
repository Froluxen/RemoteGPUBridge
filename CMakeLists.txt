cmake_minimum_required(VERSION 3.20)

project(AndroidRemoteGPUBridge
    VERSION 0.1.0
    DESCRIPTION "Android Remote GPU Bridge (ARGB) - remote rendering POC"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(pc-server)
