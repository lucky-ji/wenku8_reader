#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
strBuild="$1"
strQt="$2"
strOutput="$3"
strRoot="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$strOutput"
strOutput="$(cd "$strOutput" && pwd)"
strApp="$strOutput/轻小说文库.app"
cp -R "$strBuild/app/wenku8_client.app" "$strApp"
"$strQt/bin/macdeployqt" "$strApp" -always-overwrite -verbose=2
mkdir -p "$strApp/Contents/Resources/licenses"
cp -R "$strRoot/licenses/." "$strApp/Contents/Resources/licenses/"
cp "$strRoot/LICENSE" "$strApp/Contents/Resources/LICENSE"
cp "$strRoot/THIRD_PARTY_NOTICES.md" "$strApp/Contents/Resources/THIRD_PARTY_NOTICES.md"
strIconset="$strOutput/AppIcon.iconset"
mkdir -p "$strIconset"
for nSize in 16 32 128 256 512
 do
    sips -z "$nSize" "$nSize" "$strRoot/app/resources/app-icon.png" --out "$strIconset/icon_${nSize}x${nSize}.png" >/dev/null
    nDouble=$((nSize * 2))
    sips -z "$nDouble" "$nDouble" "$strRoot/app/resources/app-icon.png" --out "$strIconset/icon_${nSize}x${nSize}@2x.png" >/dev/null
 done
iconutil -c icns "$strIconset" -o "$strApp/Contents/Resources/AppIcon.icns"
if ! /usr/libexec/PlistBuddy -c 'Set :CFBundleIconFile AppIcon.icns' "$strApp/Contents/Info.plist"
then
    /usr/libexec/PlistBuddy -c 'Add :CFBundleIconFile string AppIcon.icns' "$strApp/Contents/Info.plist"
fi
python3 - "$strRoot" "$strQt" "$strApp" "$strOutput" <<'PY'
import json
from pathlib import Path
import subprocess
import sys
pRoot,pQt,pApp,pOutput=map(Path,sys.argv[1:])
def command(vArguments):
    return subprocess.check_output(vArguments,text=True).strip()
vMetadata={"version":"1.0.0","platform":"macos","architecture":command(["uname","-m"]),"commit":command(["git","-C",str(pRoot),"rev-parse","HEAD"]),"tree":command(["git","-C",str(pRoot),"rev-parse","HEAD^{tree}"]),"qtVersion":command([str(pQt/"bin/qmake"),"-query","QT_VERSION"]),"curlTlsBackend":"Apple SecureTransport"}
vMetadata["bundledQtFrameworks"]=sorted(pPath.name for pPath in (pApp/"Contents/Frameworks").glob("*.framework"))
vHelpers=[pPath for pPath in pApp.rglob("QtWebEngineProcess") if pPath.is_file()]
if not vHelpers:
    raise RuntimeError("Real Qt WebEngine helper missing from deployed app")
vMetadata["webEngineHelperArchitectures"]=[command(["lipo","-archs",str(pPath)]) for pPath in vHelpers]
if command(["lipo","-archs",str(pApp/"Contents/MacOS/wenku8_client")])!="x86_64":
    raise RuntimeError("Unexpected macOS application architecture")
strMetadata=json.dumps(vMetadata,indent=2)+"\n"
(pApp/"Contents/Resources/BUILD-METADATA.json").write_text(strMetadata,encoding="utf-8")
(pOutput/"BUILD-METADATA.json").write_text(strMetadata,encoding="utf-8")
PY
# Ad-hoc signature is local integrity signing, not Apple notarization.
codesign --force --deep --sign - "$strApp"
codesign --verify --deep --strict "$strApp"
"$strApp/Contents/MacOS/wenku8_client" --version
strArchive="$strOutput/wenku8-reader-1.0.0-macos-x86_64.zip"
ditto -c -k --sequesterRsrc --keepParent "$strApp" "$strArchive"
(cd "$strOutput" && shasum -a 256 "$(basename "$strArchive")" > SHA256SUMS.txt)
