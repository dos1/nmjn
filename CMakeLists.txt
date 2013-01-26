cmake_minimum_required(VERSION 2.8)

project(nmjn C)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -g")

SET(SRC_LIST main.c)

add_executable(nmjn ${SRC_LIST})
