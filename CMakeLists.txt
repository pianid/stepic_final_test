cmake_minimum_required(VERSION 2.8)
project(final)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -lpthread")

set(SOURCE_FILES main.cpp)
add_executable(final ${SOURCE_FILES})