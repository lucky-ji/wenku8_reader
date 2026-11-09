# SPDX-License-Identifier: GPL-2.0-only
if(ANDROID)
    set(QT_ANDROID_APPLICATION_BINARY wenku8_client)
    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../platform/android")
    set(ANDROID_VERSION_CODE 4)
    set(ANDROID_VERSION_NAME "${PROJECT_VERSION}")
endif()

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Svg Concurrent)
if(ANDROID)
    find_package(Qt5 5.15 REQUIRED COMPONENTS AndroidExtras)
endif()

add_library(reader_widgets STATIC
    widgets/Theme.cpp widgets/Theme.h
    widgets/Icons.cpp widgets/Icons.h
    widgets/ThemeSelector.cpp widgets/ThemeSelector.h
    widgets/FlowLayout.h
    widgets/AnimatedStack.cpp widgets/AnimatedStack.h
    widgets/TouchScroll.cpp widgets/TouchScroll.h
    widgets/LoadingPanel.cpp widgets/LoadingPanel.h
    widgets/DownloadOption.cpp widgets/DownloadOption.h
    widgets/PullRefreshIndicator.cpp widgets/PullRefreshIndicator.h
    resources/icons.qrc
)
target_include_directories(reader_widgets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/widgets")
target_link_libraries(reader_widgets PUBLIC Qt5::Widgets Qt5::Svg)
set_target_properties(reader_widgets PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC ON AUTORCC ON)

add_library(wenku8_reader STATIC
    reader/Reader.cpp reader/Reader.h
    reader/ReadingState.cpp reader/ReadingState.h
    services/CoverStore.cpp services/CoverStore.h
    services/ClientNetwork.cpp services/ClientNetwork.h
    services/NetworkRetry.h services/RasterImage.h
    services/AppInfo.cpp services/AppInfo.h
    services/DesktopNotices.cpp services/DesktopNotices.h
    resources/app_info.qrc
)
target_include_directories(wenku8_reader PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/reader"
    "${CMAKE_CURRENT_SOURCE_DIR}/services"
)
target_link_libraries(wenku8_reader PUBLIC Wenku8::Sdk reader_widgets)
set_target_properties(wenku8_reader PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC ON AUTORCC ON)

# Platform hosts depend on SDK + widgets only, never on application services/UI.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../platform" "${CMAKE_CURRENT_BINARY_DIR}/platform")

add_library(wenku8_client_services STATIC
    services/OnlineSession.cpp services/OnlineSession.h
)
target_include_directories(wenku8_client_services PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/services")
target_link_libraries(wenku8_client_services PUBLIC wenku8_reader reader_platform Qt5::Concurrent)
target_compile_definitions(wenku8_client_services PUBLIC WENKU8_CLIENT_ONLINE)
set_target_properties(wenku8_client_services PROPERTIES POSITION_INDEPENDENT_CODE ON AUTOMOC ON)

add_library(wenku8_client_ui STATIC
    ui/ClientWindow.cpp ui/ClientWindow.h
    ui/MobilePages.cpp ui/DesktopInformation.cpp ui/ReaderContents.cpp ui/DownloadActions.cpp
)
target_include_directories(wenku8_client_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/ui")
target_link_libraries(wenku8_client_ui PUBLIC wenku8_client_services)
set_target_properties(wenku8_client_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ANDROID)
    add_library(wenku8_client SHARED main.cpp)
    if(TARGET apk)
        add_dependencies(apk wenku8_client)
    endif()
    if(TARGET aab)
        add_dependencies(aab wenku8_client)
    endif()
else()
    add_executable(wenku8_client WIN32 MACOSX_BUNDLE main.cpp)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Network)
    target_link_libraries(wenku8_client PRIVATE Qt5::Network)
endif()
target_link_libraries(wenku8_client PRIVATE wenku8_client_ui)
set_target_properties(wenku8_client PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.lucky-ji.wenku8-reader"
    MACOSX_BUNDLE_BUNDLE_NAME "轻小说文库"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
foreach(strTarget IN ITEMS reader_widgets wenku8_reader reader_platform wenku8_client_services wenku8_client_ui wenku8_client)
    target_compile_features(${strTarget} PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(${strTarget} PRIVATE /W4 /utf-8)
    endif()
endforeach()
install(TARGETS wenku8_client
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    BUNDLE DESTINATION .
)

if(WIN32)
    target_sources(wenku8_client PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../platform/windows/app.rc")
endif()
