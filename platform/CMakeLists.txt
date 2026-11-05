# SPDX-License-Identifier: GPL-2.0-only
set(strAppResources "${CMAKE_CURRENT_SOURCE_DIR}/../app/resources")
add_library(reader_platform STATIC LoginHost.h "${strAppResources}/mobile_browser.qrc")
target_include_directories(reader_platform PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/android/nativebridge"
)
target_link_libraries(reader_platform PUBLIC Wenku8::Sdk reader_widgets)
set_target_properties(reader_platform PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC ON AUTORCC ON)
if(ANDROID)
    find_package(Qt5 5.15 REQUIRED COMPONENTS AndroidExtras)
    target_sources(reader_platform PRIVATE
        android/nativebridge/AndroidLoginHost.cpp
        android/nativebridge/AndroidKeys.cpp android/nativebridge/AndroidKeys.h
        android/nativebridge/MobileBrowserProtocol.cpp android/nativebridge/MobileBrowserProtocol.h
    )
    target_link_libraries(reader_platform PUBLIC Qt5::AndroidExtras)
    target_sources(wenku8_reader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/android/nativebridge/AndroidAssetTransport.cpp")
    target_link_libraries(wenku8_reader PRIVATE Qt5::AndroidExtras)
    target_compile_definitions(wenku8_reader PRIVATE WENKU8_ANDROID_ASSET_TLS)
elseif(IOS)
    message(FATAL_ERROR "iOS WKWebView session host is not implemented yet; the iOS files currently document UI groundwork only.")
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS WebEngineWidgets)
    add_library(Wenku8WebView STATIC desktop/qtwebview/Wenku8WebView.cpp desktop/qtwebview/Wenku8WebView.h)
    add_library(Wenku8::WebView ALIAS Wenku8WebView)
    target_include_directories(Wenku8WebView PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/desktop/qtwebview")
    target_link_libraries(Wenku8WebView PUBLIC Wenku8::Sdk Qt5::WebEngineWidgets Qt5::Widgets)
    target_compile_features(Wenku8WebView PUBLIC cxx_std_17)
    set_target_properties(Wenku8WebView PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(Wenku8WebView PRIVATE /W4 /utf-8)
    endif()
    target_sources(reader_platform PRIVATE desktop/DesktopLoginHost.cpp)
    target_link_libraries(reader_platform PUBLIC Wenku8::WebView)
endif()
