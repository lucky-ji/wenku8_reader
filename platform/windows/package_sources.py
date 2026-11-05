# SPDX-License-Identifier: GPL-2.0-only
"""Build a release source archive from clean project sources and matching dependencies."""
from pathlib import Path
import argparse
from collections import defaultdict
import hashlib
import json
import zipfile

vProjectFiles = (
    "README.md", "THIRD_PARTY_NOTICES.md", "LICENSE", "AGENTS.md",
    ".clang-format", ".gitattributes", ".gitignore", "CMakeLists.txt", "CMakePresets.json",
)
vProjectDirectories = ("sdk", "app", "platform", "cmake", "third_party", "licenses", "docs")
vWindowsModules = (
    "qtbase", "qtimageformats", "qtsvg", "qtdeclarative", "qtwebchannel",
    "qtlocation", "qtserialport", "qtwebengine", "qttranslations",
)
vAndroidModules = ("qtbase", "qtimageformats", "qtsvg", "qtandroidextras")
vExcludedParts = {".git", ".vs", ".vscode", "__pycache__", "CMakeFiles", "android-build", "node_modules"}
vExcludedSuffixes = {".pyc", ".pyo", ".dll", ".lib", ".obj", ".o", ".pdb", ".user", ".apk", ".aab", ".msi", ".exe", ".so", ".dylib", ".a", ".jks", ".keystore", ".p12", ".pfx", ".key", ".log"}


def fileHash(pPath):
    oHash = hashlib.sha256()
    with pPath.open("rb") as oInput:
        for vChunk in iter(lambda: oInput.read(1024 * 1024), b""):
            oHash.update(vChunk)
    return oHash.hexdigest()


def projectSources(pRoot):
    vFiles = [pRoot / strName for strName in vProjectFiles]
    for strDirectory in vProjectDirectories:
        vFiles.extend((pRoot / strDirectory).rglob("*"))
    for pFile in sorted(set(vFiles)):
        if not pFile.is_file():
            continue
        pRelative = pFile.relative_to(pRoot)
        if any(strPart in vExcludedParts for strPart in pRelative.parts):
            continue
        if pFile.suffix.lower() in vExcludedSuffixes or pFile.name == "CMakeUserPresets.json":
            continue
        yield pFile, "project/" + pRelative.as_posix()


def selectedDependency(strName, vModules, strPlatform):
    if not strName.startswith("dependencies/") or strName.endswith("/"):
        return False
    if strName.startswith("dependencies/qt-5.15.2/"):
        vParts = strName.split("/")
        return len(vParts) >= 4 and vParts[2] in vModules
    strLeaf = strName.split("/")[-1]
    if strPlatform == "android":
        return strLeaf in {"libiconv-1.17.tar.gz", "mbedtls-3.6.5.tar.bz2", "mbedtls-3.6.5-sha256sum.txt"}
    return strLeaf in {"openssl-1.1.1t.tar.gz", "zlib-1.2.13.tar.gz"}


def validateArchive(pArchive, strPlatform):
    with zipfile.ZipFile(pArchive) as oArchive:
        vNames = set(oArchive.namelist())
        if len(vNames) != len(oArchive.infolist()):
            raise RuntimeError("Duplicate source archive paths")
        strBad = oArchive.testzip()
        if strBad:
            raise RuntimeError("CRC failure: " + strBad)
        vRequired = [
            "project/LICENSE", "project/AGENTS.md", "project/.clang-format", "project/.gitattributes",
            "project/CMakeLists.txt", "project/CMakePresets.json",
            "project/sdk/CMakeLists.txt", "project/sdk/include/Wenku8Sdk.h",
            "project/app/CMakeLists.txt", "project/platform/CMakeLists.txt",
            "project/third_party/curl-8.0.1.zip", "project/third_party/libxml2-2.15.4.tar.xz",
            "dependencies/qt-5.15.2/qtbase/configure.json", "SOURCE-README.txt", "SOURCE-MANIFEST.json",
        ]
        if strPlatform == "windows":
            vRequired.extend([
                "dependencies/qt-5.15.2/qtwebengine/src/3rdparty/chromium/LICENSE",
                "dependencies/openssl-1.1.1t.tar.gz", "dependencies/zlib-1.2.13.tar.gz",
            ])
        else:
            vRequired.extend([
                "dependencies/qt-5.15.2/qtandroidextras/LICENSE.GPL2",
                "dependencies/libiconv-1.17.tar.gz", "dependencies/mbedtls-3.6.5.tar.bz2",
                "project/platform/android/AndroidManifest.xml",
                "project/third_party/libcxx-filesystem/LICENSE.TXT",
            ])
        vMissing = [strName for strName in vRequired if strName not in vNames]
        if vMissing:
            raise RuntimeError("Missing required sources: " + ", ".join(vMissing))
        vForbidden = ("project/out/", "project/build", "project/client/", "project/Wenku8/", "project/qtwebview/", "project/tests/", "project/QT/")
        if any(strName.startswith(vForbidden) for strName in vNames):
            raise RuntimeError("Unexpected development or legacy project tree")
    print("Validated ZIP CRC and required source paths", flush=True)


def runPackaging(strDefaultPlatform="windows"):
    oParser = argparse.ArgumentParser(description=__doc__)
    oParser.add_argument("--platform", choices=("windows", "android"), default=strDefaultPlatform)
    oParser.add_argument("--previous", type=Path, required=True, help="Matching dependency source archive; its old project tree is ignored")
    oParser.add_argument("--qt-source", type=Path, help="Matching Qt 5.15.2 source root, used for supermodule build files only")
    oParser.add_argument("--stage", type=Path, help="Optional deployed application directory for Qt component notices")
    oParser.add_argument("--output", type=Path, required=True)
    oParser.add_argument("--openssl-source", type=Path, help="Windows matching OpenSSL 1.1.1t original archive")
    oParser.add_argument("--zlib-source", type=Path, help="Windows matching zlib 1.2.13 original archive")
    oArguments = oParser.parse_args()
    pRoot = Path(__file__).resolve().parents[2]
    pOutput = oArguments.output.resolve()
    pPrevious = oArguments.previous.resolve()
    if pOutput == pPrevious:
        raise RuntimeError("Output must differ from previous archive")
    pOutput.parent.mkdir(parents=True, exist_ok=True)
    vModules = vWindowsModules if oArguments.platform == "windows" else vAndroidModules
    vProjectManifest = []
    vDependencyManifest = defaultdict(lambda: {"files": 0, "bytes": 0})
    vNotices = []
    pNoticeDirectory = oArguments.stage / "licenses/qt-components" if oArguments.stage else None
    if pNoticeDirectory:
        pNoticeDirectory.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(pOutput, "w", zipfile.ZIP_DEFLATED, compresslevel=1) as oOutput:
        with zipfile.ZipFile(pPrevious) as oPrevious:
            nCopied = 0
            for oEntry in oPrevious.infolist():
                strName = oEntry.filename
                if not selectedDependency(strName, vModules, oArguments.platform):
                    continue
                vData = oPrevious.read(oEntry)
                oOutput.writestr(strName, vData)
                strGroup = "/".join(strName.split("/")[:3])
                vDependencyManifest[strGroup]["files"] += 1
                vDependencyManifest[strGroup]["bytes"] += len(vData)
                if pNoticeDirectory and strName.startswith("dependencies/qt-5.15.2/"):
                    pName = Path(strName)
                    if pName.name.lower().startswith(("license", "copying", "copyright", "notice")) or pName.name == "qt_attribution.json":
                        strNotice = pName.parts[2] + "-" + hashlib.sha256(strName.encode()).hexdigest()[:12] + "-" + pName.name[:70] + ".txt"
                        (pNoticeDirectory / strNotice).write_bytes(vData)
                        vNotices.append(strName + "\t" + strNotice)
                nCopied += 1
                if nCopied % 10000 == 0:
                    print("Copied matching dependency entries:", nCopied, flush=True)
        if oArguments.qt_source:
            for pFile in sorted(oArguments.qt_source.iterdir()):
                if pFile.is_file() and not pFile.name.startswith(".QT-"):
                    oOutput.write(pFile, "dependencies/qt-5.15.2/" + pFile.name)
        if oArguments.platform == "windows":
            for pSource, strName in [
                (oArguments.openssl_source or pRoot / "openssl-1.1.1t.tar.gz", "openssl-1.1.1t.tar.gz"),
                (oArguments.zlib_source or pRoot / "zlib-1.2.13.tar.gz", "zlib-1.2.13.tar.gz"),
            ]:
                if "dependencies/" + strName in vDependencyManifest:
                    continue
                if not pSource.is_file():
                    raise RuntimeError("Missing matching dependency archive: " + strName)
                oOutput.write(pSource, "dependencies/" + strName)
                vDependencyManifest["dependencies/" + strName] = {"files": 1, "bytes": pSource.stat().st_size, "sha256": fileHash(pSource)}
        for pFile, strName in projectSources(pRoot):
            vData = pFile.read_bytes()
            oOutput.writestr(strName, vData)
            vProjectManifest.append({"path": strName, "sha256": hashlib.sha256(vData).hexdigest(), "bytes": len(vData)})
        strPlatformNotes = (
            "Windows Qt WebEngine sources include Chromium and its original third-party notices. "
            "OpenSSL 1.1.1t and zlib 1.2.13 sources match deployed Qt dependencies; the SDK cURL uses Windows Schannel. "
            "Microsoft CRT/D3D runtime components are redistributables under their own terms."
            if oArguments.platform == "windows" else
            "Android sources include Qt Core/Gui/Widgets/Concurrent, Svg, image formats, AndroidExtras, "
            "Mbed TLS 3.6.5, GNU libiconv/libcharset 1.17 and matching LLVM filesystem source. "
            "System WebView is supplied by Android and is not bundled; Android does not bundle OpenSSL or Chromium."
        )
        strReadme = (
            "Light Novel Library / 轻小说文库 1.0.0 - " + oArguments.platform + " corresponding source\n\n"
            "project/ contains the clean C++ SDK, Qt 5 client, platform adapters, build scripts and licenses.\n"
            "dependencies/ contains matching, unmodified third-party sources and original notices.\n"
            + strPlatformNotes + "\n\n"
            "Build instructions: project/docs/BUILDING.md. Project/SDK license: GPL-2.0-only.\n"
            "Third-party components retain their original copyright and licenses.\n"
            "Qt source trees intentionally retain upstream tests and fixtures required to preserve original sources; "
            "these are not application debug tools. Qt/VS/SDK binaries and local toolchain installations are not included.\n"
            "SOURCE-MANIFEST.json records project hashes and dependency origin.\n"
            "No account data, cookies, reading library, signing keys, local build output or old application tree is included.\n"
        )
        oOutput.writestr("SOURCE-README.txt", strReadme)
        oOutput.writestr("SOURCE-MANIFEST.json", json.dumps({
            "version": "1.0.0", "platform": oArguments.platform,
            "dependencyOrigin": {"archive": pPrevious.name, "sha256": fileHash(pPrevious)},
            "dependencyTrees": dict(vDependencyManifest), "project": vProjectManifest,
        }, ensure_ascii=False, indent=2) + "\n")
    if pNoticeDirectory:
        (pNoticeDirectory / "INDEX.txt").write_text("\n".join(vNotices) + "\n", encoding="utf-8")
    validateArchive(pOutput, oArguments.platform)
    pOutput.with_suffix(pOutput.suffix + ".sha256").write_text(fileHash(pOutput) + "  " + pOutput.name + "\n", encoding="utf-8")
    print("Source archive:", pOutput.name, "bytes:", pOutput.stat().st_size, flush=True)


if __name__ == "__main__":
    runPackaging()
