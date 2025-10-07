# The following lines of boilerplate have to be in your project's
# CMakeLists.txt for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Set the project name
set(PROJECT_NAME "esp32-rfid-access-control")

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set the project name for ESP-IDF
set(PROJECT_NAME ${PROJECT_NAME})

# Project configuration
project(${PROJECT_NAME})