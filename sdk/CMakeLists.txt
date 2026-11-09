# SPDX-License-Identifier: GPL-2.0-only
cmake_minimum_required(VERSION 3.24)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(Wenku8Sdk VERSION 1.0.2 LANGUAGES C CXX)
endif()

include(GNUInstallDirs)

if(ANDROID OR IOS)
    set(bSdkSharedDefault OFF)
else()
    set(bSdkSharedDefault ON)
endif()

option(WENKU8_BUILD_SHARED "Build the pure C++ SDK as a shared library" ${bSdkSharedDefault})

# The dependency module belongs to the repository, not to the application.
# SDK-only builds enter here without locating or loading any Qt package.
if(NOT TARGET CURL::libcurl OR NOT TARGET LibXml2::LibXml2)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../cmake/Dependencies.cmake")
        include("${CMAKE_CURRENT_LIST_DIR}/../cmake/Dependencies.cmake")
    else()
        find_package(CURL 7.85 REQUIRED)
        find_package(LibXml2 REQUIRED)
    endif()
endif()

if(TARGET wenku8_xml)
    set(strSdkXmlTarget wenku8_xml)
elseif(TARGET tinyxml2::tinyxml2)
    set(strSdkXmlTarget tinyxml2::tinyxml2)
elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../third_party/tinyxml2/tinyxml2.cpp")
    add_library(wenku8_xml STATIC "${CMAKE_CURRENT_LIST_DIR}/../third_party/tinyxml2/tinyxml2.cpp")
    target_include_directories(wenku8_xml PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../third_party/tinyxml2")
    set_target_properties(wenku8_xml PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set(strSdkXmlTarget wenku8_xml)
else()
    find_package(tinyxml2 CONFIG REQUIRED)
    set(strSdkXmlTarget tinyxml2::tinyxml2)
endif()

if(WENKU8_BUILD_SHARED)
    add_library(Wenku8Sdk SHARED)
else()
    add_library(Wenku8Sdk STATIC)
    target_compile_definitions(Wenku8Sdk PUBLIC WENKU8_STATIC)
endif()

add_library(Wenku8::Sdk ALIAS Wenku8Sdk)

target_sources(Wenku8Sdk PRIVATE
    src/Wenku8Sdk.cpp
    src/Wenku8Api.cpp
    src/HttpTransport.cpp
    src/RetryTransport.cpp
    src/FileCache.cpp
    src/WebBackend.cpp
    src/TextEncoding.cpp
    src/Wenku8Catalog.cpp
    src/TextPackage.cpp
    src/Wenku8Download.cpp
    src/Wenku8Export.cpp
)

target_compile_features(Wenku8Sdk PUBLIC cxx_std_17)
target_compile_definitions(Wenku8Sdk PRIVATE WENKU8SDK_EXPORTS)
set_target_properties(Wenku8Sdk PROPERTIES
    VERSION 1.0.2
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(Wenku8Sdk
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/wenku8>"
    PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src"
)

target_link_libraries(Wenku8Sdk PRIVATE
    CURL::libcurl
    LibXml2::LibXml2
    ${strSdkXmlTarget}
)

if(NOT WIN32)
    find_package(Iconv REQUIRED)
    target_link_libraries(Wenku8Sdk PRIVATE Iconv::Iconv)
endif()

set(WENKU8_ANDROID_FILESYSTEM_SOURCE_DIR "" CACHE PATH "Matching libc++ filesystem sources for Android NDK r21")
if(ANDROID AND WENKU8_ANDROID_FILESYSTEM_SOURCE_DIR)
    set(vSdkFilesystemSources
        "${WENKU8_ANDROID_FILESYSTEM_SOURCE_DIR}/src/filesystem/operations.cpp"
        "${WENKU8_ANDROID_FILESYSTEM_SOURCE_DIR}/src/filesystem/directory_iterator.cpp"
    )
    foreach(strSdkFilesystemSource IN LISTS vSdkFilesystemSources)
        if(NOT EXISTS "${strSdkFilesystemSource}")
            message(FATAL_ERROR "The selected Android filesystem sources are incomplete")
        endif()
    endforeach()
    target_sources(Wenku8Sdk PRIVATE ${vSdkFilesystemSources})
    set_source_files_properties(${vSdkFilesystemSources}
        PROPERTIES COMPILE_DEFINITIONS "_LIBCPP_BUILDING_LIBRARY"
    )
endif()

if(MSVC)
    target_compile_options(Wenku8Sdk PRIVATE /W4 /utf-8)
else()
    target_compile_options(Wenku8Sdk PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS Wenku8Sdk
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES
    include/Wenku8Sdk.h
    include/Wenku8Api.h
    include/Wenku8Catalog.h
    include/Wenku8Download.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/wenku8
)
