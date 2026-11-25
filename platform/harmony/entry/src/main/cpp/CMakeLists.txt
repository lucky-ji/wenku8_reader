# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.24)
project(Wenku8HarmonyBridge LANGUAGES C CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/HarmonySdk.cmake")

add_library(wenku8_bridge SHARED Bridge.cpp)
target_compile_features(wenku8_bridge PRIVATE cxx_std_17)
target_compile_options(wenku8_bridge PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(wenku8_bridge PRIVATE Wenku8::Sdk libace_napi.z.so)
