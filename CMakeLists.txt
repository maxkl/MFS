cmake_minimum_required(VERSION 3.6)
project(MFS)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

set(SOURCE_FILES main.c mfs.c mfs.h)
add_executable(MFS ${SOURCE_FILES})