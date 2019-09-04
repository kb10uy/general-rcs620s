cmake_minimum_required(VERSION 3.9)
project(general-rcs620s)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

add_library(
  rcs620s
  RCS620S.cpp
)

target_include_directories(rcs620s SYSTEM PRIVATE $ENV{INCLUDE})
