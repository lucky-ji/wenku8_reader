# SPDX-License-Identifier: GPL-2.0-only
include_guard(GLOBAL)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    message(FATAL_ERROR "HarmonySdk.cmake requires the DevEco OHOS native toolchain")
endif()

include(FetchContent)
get_filename_component(strWenku8Root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

set(WENKU8_HARMONY_MBEDTLS_ARCHIVE "$ENV{WENKU8_HARMONY_MBEDTLS_ARCHIVE}" CACHE FILEPATH
    "Optional local copy of the audited Mbed TLS 3.6.5 release archive")
set(strMbedTlsArchive
    "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2")
if(WENKU8_HARMONY_MBEDTLS_ARCHIVE)
    if(NOT EXISTS "${WENKU8_HARMONY_MBEDTLS_ARCHIVE}")
        message(FATAL_ERROR "The specified Mbed TLS release archive does not exist")
    endif()
    set(strMbedTlsArchive "${WENKU8_HARMONY_MBEDTLS_ARCHIVE}")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
# DevEco adds a redundant GCC toolchain flag to Clang's compile commands.
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(wenku8_harmony_mbedtls
    URL "${strMbedTlsArchive}"
    URL_HASH SHA256=4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    TLS_VERIFY TRUE
)
FetchContent_MakeAvailable(wenku8_harmony_mbedtls)

# curl 8.0.1 uses a find-module instead of Mbed TLS's exported targets.
set(MBEDTLS_INCLUDE_DIRS "${wenku8_harmony_mbedtls_SOURCE_DIR}/include")
set(MBEDTLS_LIBRARY mbedtls)
set(MBEDX509_LIBRARY mbedx509)
set(MBEDCRYPTO_LIBRARY mbedcrypto)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
set(WENKU8_BUNDLED_CURL ON CACHE BOOL "" FORCE)
set(WENKU8_BUILD_SHARED OFF CACHE BOOL "" FORCE)

add_subdirectory("${strWenku8Root}/sdk" "${CMAKE_CURRENT_BINARY_DIR}/wenku8-sdk" EXCLUDE_FROM_ALL)
