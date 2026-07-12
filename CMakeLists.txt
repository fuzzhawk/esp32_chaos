# chaosOS — experimental research OS for ESP32-S3-Touch-AMOLED-1.75
# Top-level ESP-IDF project file.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Pull local components (bsp, chaos_os, apps) in addition to managed ones.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

project(chaos_os)
