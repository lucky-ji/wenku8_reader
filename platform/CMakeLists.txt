# SPDX-License-Identifier: GPL-2.0-only
set(strAppResources "${CMAKE_CURRENT_SOURCE_DIR}/../app/resources")
add_library(reader_platform STATIC LoginHost.h ExportDestination.h SpeechEngine.h "${strAppResources}/mobile_browser.qrc")
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
        android/nativebridge/AndroidExportDestination.cpp
        android/nativebridge/AndroidKeys.cpp android/nativebridge/AndroidKeys.h
        android/nativebridge/AndroidSpeechEngine.cpp
        android/nativebridge/MobileBrowserProtocol.cpp android/nativebridge/MobileBrowserProtocol.h
    )
    target_link_libraries(reader_platform PUBLIC Qt5::AndroidExtras)
    target_sources(wenku8_reader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/android/nativebridge/AndroidAssetTransport.cpp")
    target_link_libraries(wenku8_reader PRIVATE Qt5::AndroidExtras)
    target_compile_definitions(wenku8_reader PRIVATE WENKU8_ANDROID_ASSET_TLS)
elseif(IOS)
    find_package(Qt5 5.15 REQUIRED COMPONENTS TextToSpeech)
    find_library(strFoundationFramework Foundation REQUIRED)
    find_library(strUIKitFramework UIKit REQUIRED)
    find_library(strWebKitFramework WebKit REQUIRED)
    target_sources(reader_platform PRIVATE
        ios/IosLoginHost.mm
        android/nativebridge/MobileBrowserProtocol.cpp android/nativebridge/MobileBrowserProtocol.h
        desktop/ExportDestination.cpp
        desktop/QtSpeechEngine.cpp
    )
    set_source_files_properties(ios/IosLoginHost.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_target_properties(reader_platform PROPERTIES OBJCXX_STANDARD 17 OBJCXX_STANDARD_REQUIRED ON)
    target_link_libraries(reader_platform
        PUBLIC Qt5::TextToSpeech
        PRIVATE "${strFoundationFramework}" "${strUIKitFramework}" "${strWebKitFramework}"
    )
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS WebEngineWidgets TextToSpeech)
    add_library(Wenku8WebView STATIC desktop/qtwebview/Wenku8WebView.cpp desktop/qtwebview/Wenku8WebView.h)
    add_library(Wenku8::WebView ALIAS Wenku8WebView)
    target_include_directories(Wenku8WebView PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/desktop/qtwebview")
    target_link_libraries(Wenku8WebView PUBLIC Wenku8::Sdk Qt5::WebEngineWidgets Qt5::Widgets)
    target_compile_features(Wenku8WebView PUBLIC cxx_std_17)
    set_target_properties(Wenku8WebView PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(Wenku8WebView PRIVATE /W4 /utf-8)
    endif()
    target_sources(reader_platform PRIVATE desktop/DesktopLoginHost.cpp desktop/ExportDestination.cpp desktop/QtSpeechEngine.cpp)
    target_link_libraries(reader_platform PUBLIC Wenku8::WebView Qt5::TextToSpeech)
endif()
