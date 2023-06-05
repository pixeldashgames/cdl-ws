cmake_minimum_required(VERSION 3.16)
project(cdl_ws C)

set(CMAKE_C_STANDARD 11)

add_executable(cdl_ws cdl-utils.c cdl-ws.c)
