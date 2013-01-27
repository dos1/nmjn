cmake_minimum_required(VERSION 2.8)

project(nmjn C)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -g")

include_directories(${GTK3_INCLUDE_DIRS})

link_directories(${GTK3_LIBRARY_DIRS})

add_definitions(${GTK3_CFLAGS_OTHER})

SET(SRC_LIST main.c)

add_executable(nmjn ${SRC_LIST})

target_link_libraries(nmjn ${GTK3_LIBRARIES})
