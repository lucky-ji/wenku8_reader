#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

strRoot=$(cd "$(dirname "$0")/../.." && pwd)
strTarget=${1:-simulator}
strBuild=${2:-"$strRoot/build/ios-$strTarget"}
case "$strTarget" in
    simulator)
        strSdk=iphonesimulator
        strArchitecture=x86_64
        ;;
    device)
        strSdk=iphoneos
        strArchitecture=arm64
        ;;
    *)
        echo "Usage: bash platform/ios/build.sh [simulator|device] [build-directory]" >&2
        exit 2
        ;;
esac
: "${QT5_IOS_ROOT:?Set QT5_IOS_ROOT to the Qt 5.15.2 iOS SDK directory.}"

test "$(uname -s)" = Darwin
test -f "$QT5_IOS_ROOT/lib/cmake/Qt5/Qt5Config.cmake"
xcrun --sdk "$strSdk" --show-sdk-path >/dev/null

cmake -S "$strRoot" -B "$strBuild" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$strSdk" \
    -DCMAKE_OSX_ARCHITECTURES="$strArchitecture" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_CONFIGURATION_TYPES=Release \
    -DCMAKE_PREFIX_PATH="$QT5_IOS_ROOT" \
    -DCMAKE_FIND_ROOT_PATH="$QT5_IOS_ROOT" \
    -DCMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE=NO \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED=NO \
    -DWENKU8_BUILD_CLIENT=ON -DWENKU8_BUILD_SHARED=OFF \
    -DWENKU8_BUNDLED_CURL=ON \
    -DCURL_USE_OPENSSL=OFF -DCURL_USE_MBEDTLS=OFF -DCURL_USE_SECTRANSP=ON \
    -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON
cmake --build "$strBuild" --config Release --target wenku8_client

strApp=$(find "$strBuild" -type d -name wenku8_client.app -print -quit)
if [[ ! -d "$strApp" ]]; then
    echo "The build did not produce wenku8_client.app." >&2
    exit 1
fi
strExecutable=$(plutil -extract CFBundleExecutable raw "$strApp/Info.plist")
test "$(plutil -extract CFBundleIdentifier raw "$strApp/Info.plist")" = io.github.lucky-ji.wenku8-reader
test "$(xcrun lipo -archs "$strApp/$strExecutable")" = "$strArchitecture"
test -d "$strApp/LaunchScreen.storyboardc"
echo "Verified $strTarget build: $strApp"
