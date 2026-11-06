# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.24)
project(Wenku8Reader VERSION 1.0.1 LANGUAGES C CXX)

option(WENKU8_BUILD_CLIENT "Build the Qt 5 reader application" OFF)
add_subdirectory(sdk)
if(WENKU8_BUILD_CLIENT)
    add_subdirectory(app)
endif()

include(GNUInstallDirs)
install(DIRECTORY licenses/ DESTINATION ${CMAKE_INSTALL_DATADIR}/wenku8/licenses)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/wenku8)
