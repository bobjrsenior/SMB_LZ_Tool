cmake_minimum_required(VERSION 3.6)
project(SMB_LZ_Tool)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    Main.c lzss.c)

add_executable(SMB_LZ_Tool ${SOURCE_FILES})