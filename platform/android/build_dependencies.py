#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build the Qt 5 Android SDK's native dependencies from local upstream sources."""

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tarfile
import zipfile


def parseArguments():
    oParser = argparse.ArgumentParser(description=__doc__)
    oParser.add_argument("--ndk", dest="pNdk", type=Path, required=True,
                         help="Android NDK r21e root")
    oParser.add_argument("--source-dir", dest="pSources", type=Path, required=True,
                         help="Directory containing the three upstream source trees or archives")
    oParser.add_argument("--prefix-root", dest="pPrefixes", type=Path, required=True,
                         help="Install each ABI below this directory; work stays in .build/")
    oParser.add_argument("--abi", dest="strAbi", default="all",
                         choices=["all", "arm64-v8a", "armeabi-v7a", "x86", "x86_64"])
    oParser.add_argument("--jobs", dest="nJobs", type=int, default=os.cpu_count() or 1)
    oParser.add_argument("--shell", dest="strShell",
                         help="POSIX shell executable; explicitly required on Windows")
    oParser.add_argument("--make", dest="strMake", default="make", help="GNU make executable")
    oParser.add_argument("--cmake", dest="strCmake", default="cmake")
    oParser.add_argument("--ninja", dest="strNinja", default="ninja")
    oParser.add_argument("--dry-run", dest="bDryRun", action="store_true",
                         help="Validate tools and source versions, then print the build plan")
    oArgs = oParser.parse_args()
    if oArgs.nJobs < 1:
        oParser.error("--jobs must be positive")
    if os.name == "nt" and not oArgs.strShell:
        oParser.error("Windows requires --shell pointing to a Git Bash/MSYS POSIX shell, not WSL")
    oArgs.strShell = oArgs.strShell or "sh"
    for strAttribute in ("pNdk", "pSources", "pPrefixes"):
        setattr(oArgs, strAttribute, getattr(oArgs, strAttribute).resolve())
    # Autoconf/Libtool do not support whitespace in source/build/compiler paths reliably.
    for pDirectory in (oArgs.pNdk, oArgs.pSources, oArgs.pPrefixes):
        if any(strCharacter.isspace() for strCharacter in str(pDirectory)):
            oParser.error("NDK, source and prefix paths must not contain whitespace (Autoconf)")
    for strAttribute in ("strShell", "strMake", "strCmake", "strNinja"):
        strTool = getattr(oArgs, strAttribute)
        strResolved = shutil.which(strTool)
        if not strResolved:
            oParser.error("Executable not found: " + strTool)
        setattr(oArgs, strAttribute, str(Path(strResolved).resolve()))
    if os.name == "nt" and any(strPart in oArgs.strShell.lower()
                              for strPart in ("windowsapps", "system32")):
        oParser.error("Use a Windows POSIX shell, not the Windows WSL launcher")
    oShellCheck = subprocess.run([oArgs.strShell, "-c", "printf posix-shell-ready"],
                                 capture_output=True, text=True, timeout=10)
    if oShellCheck.returncode or oShellCheck.stdout != "posix-shell-ready":
        oParser.error("--shell must execute POSIX shell commands")
    oMakeCheck = subprocess.run([oArgs.strMake, "--version"], capture_output=True,
                                text=True, timeout=10)
    if oMakeCheck.returncode or "GNU Make" not in oMakeCheck.stdout:
        oParser.error("--make must be GNU make")
    return oArgs


def sourceSpecifications():
    return (
        ("mbedtls-3.6.5", "include/mbedtls/build_info.h",
         r'#define\s+MBEDTLS_VERSION_STRING\s+"3\.6\.5"'),
        ("curl-8.0.1", "include/curl/curlver.h",
         r'#define\s+LIBCURL_VERSION\s+"8\.0\.1"'),
        ("libiconv-1.17", "configure", r"PACKAGE_VERSION='1\.17'"),
    )


def locateSource(pDirectory, strName, strMarker, strPattern):
    pTree = pDirectory / strName
    if pTree.is_dir():
        strText = (pTree / strMarker).read_text(encoding="utf-8", errors="replace")
        if not re.search(strPattern, strText):
            raise ValueError("Unexpected source version: " + str(pTree))
        return pTree
    for strSuffix in (".tar.gz", ".tar.bz2", ".tar.xz", ".zip"):
        pArchive = pDirectory / (strName + strSuffix)
        if not pArchive.is_file():
            continue
        if strSuffix == ".zip":
            with zipfile.ZipFile(pArchive) as oArchive:
                vMembers = [strMember for strMember in oArchive.namelist()
                            if strMember.endswith("/" + strMarker) and
                            len(strMember.strip("/").split("/")) == len(strMarker.split("/")) + 1]
                if len(vMembers) != 1:
                    raise ValueError("Ambiguous source archive: " + str(pArchive))
                strText = oArchive.read(vMembers[0]).decode("utf-8", errors="replace")
        else:
            with tarfile.open(pArchive) as oArchive:
                vMembers = [oMember for oMember in oArchive.getmembers()
                            if oMember.isfile() and oMember.name.endswith("/" + strMarker) and
                            len(oMember.name.strip("/").split("/")) == len(strMarker.split("/")) + 1]
                if len(vMembers) != 1:
                    raise ValueError("Ambiguous source archive: " + str(pArchive))
                with oArchive.extractfile(vMembers[0]) as oFile:
                    strText = oFile.read().decode("utf-8", errors="replace")
        if not re.search(strPattern, strText):
            raise ValueError("Unexpected source version: " + str(pArchive))
        return pArchive
    raise FileNotFoundError("Missing upstream source: " + strName)


def validateMember(pDestination, strName):
    pTarget = (pDestination / strName).resolve()
    if not pTarget.is_relative_to(pDestination.resolve()):
        raise ValueError("Source archive contains a path outside its extraction directory")


def prepareSource(pOriginal, pDestination, strMarker):
    if (pDestination / strMarker).is_file():
        return pDestination
    pDestination.parent.mkdir(parents=True, exist_ok=True)
    if pOriginal.is_dir():
        shutil.copytree(pOriginal, pDestination)
        return pDestination
    pExtraction = pDestination.parent / (pDestination.name + "-extracted")
    pExtraction.mkdir(parents=True, exist_ok=True)
    if pOriginal.suffix == ".zip":
        with zipfile.ZipFile(pOriginal) as oArchive:
            for oMember in oArchive.infolist():
                validateMember(pExtraction, oMember.filename)
                if (oMember.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError("Symbolic links are not accepted in source ZIP archives")
            oArchive.extractall(pExtraction)
    else:
        with tarfile.open(pOriginal) as oArchive:
            for oMember in oArchive.getmembers():
                validateMember(pExtraction, oMember.name)
                if not (oMember.isfile() or oMember.isdir()):
                    raise ValueError("Only regular files and directories are accepted in source TARs")
            oArchive.extractall(pExtraction, filter="data")
    vRoots = [pMarker.parent for pMarker in pExtraction.rglob(strMarker.split("/")[-1])
              if pMarker.as_posix().endswith("/" + strMarker) and
              len(pMarker.relative_to(pExtraction).parts) == len(strMarker.split("/")) + 1]
    if len(vRoots) != 1:
        raise ValueError("Could not identify the extracted source root")
    pRoot = vRoots[0]
    for strSegment in strMarker.split("/")[:-1]:
        pRoot = pRoot.parent
    shutil.copytree(pRoot, pDestination)
    return pDestination


def runCommand(vArguments, pLog, pWorkingDirectory, vEnvironment):
    print("Running " + pLog.name, flush=True)
    pLog.parent.mkdir(parents=True, exist_ok=True)
    with pLog.open("w", encoding="utf-8") as oLog:
        oResult = subprocess.run([str(oValue) for oValue in vArguments], cwd=pWorkingDirectory,
                                 env=vEnvironment, stdout=oLog, stderr=subprocess.STDOUT)
    if oResult.returncode:
        raise RuntimeError("Command failed; see " + str(pLog))


def verifyConfiguration(pCache, strAbi):
    strCache = pCache.read_text(encoding="utf-8")
    vExpected = {"ANDROID_ABI": strAbi, "ANDROID_PLATFORM": "android-21",
                 "ANDROID_STL": "c++_shared", "CMAKE_BUILD_TYPE": "Release",
                 "CMAKE_POSITION_INDEPENDENT_CODE": "ON"}
    for strName, strExpected in vExpected.items():
        oMatch = re.search(r"^" + re.escape(strName) + r":[^=]+=(.*)$", strCache, re.MULTILINE)
        if not oMatch or oMatch.group(1).strip() != strExpected:
            raise ValueError("Unexpected " + strName + " in " + str(pCache))


def verifyArchive(pArchive, nExpectedMachine):
    vBytes = pArchive.read_bytes()
    if not vBytes.startswith(b"!<arch>\n"):
        raise ValueError("Expected a static archive: " + str(pArchive))
    nOffset = 8
    nObjects = 0
    while nOffset + 60 <= len(vBytes):
        vHeader = vBytes[nOffset:nOffset + 60]
        nSize = int(vHeader[48:58].decode("ascii").strip())
        vBody = vBytes[nOffset + 60:nOffset + 60 + nSize]
        if vHeader[:3] == b"#1/":
            nNameSize = int(vHeader[3:16].decode("ascii").strip())
            vBody = vBody[nNameSize:]
        if vBody.startswith(b"\x7fELF"):
            nMachine = struct.unpack("<H", vBody[18:20])[0]
            if nMachine != nExpectedMachine:
                raise ValueError("Wrong ABI in " + str(pArchive))
            nObjects += 1
        nOffset += 60 + nSize + nSize % 2
    if not nObjects:
        raise ValueError("No ELF objects found in " + str(pArchive))


def main():
    oArgs = parseArguments()
    strRevision = (oArgs.pNdk / "source.properties").read_text(encoding="utf-8")
    if not re.search(r"Pkg\.Revision\s*=\s*21\.4\.7075529\b", strRevision):
        raise ValueError("This Qt 5 build recipe requires Android NDK r21e (21.4.7075529)")
    strHost = {"win32": "windows-x86_64", "linux": "linux-x86_64",
               "darwin": "darwin-x86_64"}.get(sys.platform)
    if not strHost:
        raise ValueError("Unsupported NDK host platform")
    pCompilerBin = oArgs.pNdk / "toolchains/llvm/prebuilt" / strHost / "bin"
    strExecutableSuffix = ".exe" if os.name == "nt" else ""
    pClang = pCompilerBin / ("clang" + strExecutableSuffix)
    pAr = pCompilerBin / ("llvm-ar" + strExecutableSuffix)
    pRanlib = pCompilerBin / ("llvm-ranlib" + strExecutableSuffix)
    for pTool in (pClang, pAr, pRanlib):
        if not pTool.is_file():
            raise FileNotFoundError(pTool)
    vSources = [(strName, strMarker, locateSource(oArgs.pSources, strName, strMarker, strPattern))
                for strName, strMarker, strPattern in sourceSpecifications()]
    vAbis = {
        "arm64-v8a": ("aarch64-linux-android21", "aarch64-linux-android", 183),
        "armeabi-v7a": ("armv7a-linux-androideabi21", "arm-linux-androideabi", 40),
        "x86": ("i686-linux-android21", "i686-linux-android", 3),
        "x86_64": ("x86_64-linux-android21", "x86_64-linux-android", 62),
    }
    vSelectedAbis = list(vAbis) if oArgs.strAbi == "all" else [oArgs.strAbi]
    print(json.dumps({"sources": {strName: str(pSource) for strName, strMarker, pSource in vSources},
                      "abis": vSelectedAbis, "api": 21, "stl": "c++_shared",
                      "configuration": "Release", "pic": True,
                      "prefixRoot": str(oArgs.pPrefixes)}, indent=2))
    if oArgs.bDryRun:
        print("Input validation passed; no files were changed or compiled.")
        return
    vEnvironment = os.environ.copy()
    vEnvironment["PATH"] = str(Path(oArgs.strMake).parent) + os.pathsep + vEnvironment["PATH"]
    pWork = oArgs.pPrefixes / ".build"
    vPreparedSources = {
        strName: prepareSource(pSource, pWork / "sources" / strName, strMarker)
        for strName, strMarker, pSource in vSources if not strName.startswith("libiconv")
    }
    for strAbi in vSelectedAbis:
        strTarget, strConfigureHost, nMachine = vAbis[strAbi]
        pPrefix = oArgs.pPrefixes / strAbi
        pBuild = pWork / strAbi
        pLogs = pBuild / "logs"
        vBaseOptions = ["-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + oArgs.strNinja,
                        "-DCMAKE_TOOLCHAIN_FILE=" +
                        (oArgs.pNdk / "build/cmake/android.toolchain.cmake").as_posix(),
                        "-DANDROID_ABI=" + strAbi, "-DANDROID_PLATFORM=android-21",
                        "-DANDROID_STL=c++_shared", "-DCMAKE_BUILD_TYPE=Release",
                        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                        "-DCMAKE_INSTALL_PREFIX=" + pPrefix.as_posix()]
        vMbedOptions = ["-DENABLE_PROGRAMS=OFF", "-DENABLE_TESTING=OFF",
                        "-DUSE_SHARED_MBEDTLS_LIBRARY=OFF", "-DUSE_STATIC_MBEDTLS_LIBRARY=ON"]
        vCurlOptions = ["-DCMAKE_PREFIX_PATH=" + pPrefix.as_posix(),
                        "-DCMAKE_FIND_ROOT_PATH=" + pPrefix.as_posix(),
                        "-DMBEDTLS_INCLUDE_DIRS=" + (pPrefix / "include").as_posix(),
                        "-DMBEDTLS_LIBRARY=" + (pPrefix / "lib/libmbedtls.a").as_posix(),
                        "-DMBEDX509_LIBRARY=" + (pPrefix / "lib/libmbedx509.a").as_posix(),
                        "-DMBEDCRYPTO_LIBRARY=" + (pPrefix / "lib/libmbedcrypto.a").as_posix(),
                        "-DBUILD_SHARED_LIBS=OFF", "-DBUILD_CURL_EXE=OFF", "-DBUILD_TESTING=OFF",
                        "-DHTTP_ONLY=ON", "-DCURL_USE_OPENSSL=OFF", "-DCURL_USE_MBEDTLS=ON",
                        "-DCURL_ZLIB=OFF", "-DCURL_BROTLI=OFF", "-DCURL_ZSTD=OFF",
                        "-DCURL_USE_LIBPSL=OFF", "-DCURL_USE_LIBSSH2=OFF", "-DUSE_LIBIDN2=OFF",
                        "-DCURL_DISABLE_NTLM=ON", "-DCURL_ENABLE_EXPORT_TARGET=ON"]
        for strComponent, strSource, vOptions in (
                ("mbedtls", "mbedtls-3.6.5", vMbedOptions),
                ("curl", "curl-8.0.1", vCurlOptions)):
            pComponent = pBuild / strComponent
            runCommand([oArgs.strCmake, "-S", vPreparedSources[strSource], "-B", pComponent,
                        *vBaseOptions, *vOptions], pLogs / (strComponent + "-configure.log"),
                       oArgs.pSources, vEnvironment)
            runCommand([oArgs.strCmake, "--build", pComponent, "--parallel", oArgs.nJobs],
                       pLogs / (strComponent + "-build.log"), oArgs.pSources, vEnvironment)
            runCommand([oArgs.strCmake, "--install", pComponent],
                       pLogs / (strComponent + "-install.log"), oArgs.pSources, vEnvironment)
            verifyConfiguration(pComponent / "CMakeCache.txt", strAbi)
        pIconvOriginal = next(pSource for strName, strMarker, pSource in vSources
                              if strName == "libiconv-1.17")
        pIconv = prepareSource(pIconvOriginal, pBuild / "libiconv-1.17", "configure")
        strCommand = (
            "export CONFIG_SHELL=/bin/sh SHELL=/bin/sh; "
            "export CC=" + shlex.quote(pClang.as_posix() + " --target=" + strTarget) +
            " AR=" + shlex.quote(pAr.as_posix()) +
            " RANLIB=" + shlex.quote(pRanlib.as_posix()) +
            " CFLAGS='-O2 -fPIC -fvisibility=hidden'; "
            "./configure --host=" + shlex.quote(strConfigureHost) +
            " --prefix=" + shlex.quote(pPrefix.as_posix()) + " --disable-shared --enable-static")
        runCommand([oArgs.strShell, "-c", strCommand], pLogs / "iconv-configure.log",
                   pIconv, vEnvironment)
        # Build libraries only. The command-line utility, gnulib tests, translations and
        # manpage installation are unnecessary for the SDK and cross-host build.
        runCommand([oArgs.strShell, "-c", shlex.quote(Path(oArgs.strMake).as_posix()) +
                    " -C libcharset/lib -j" + str(oArgs.nJobs)],
                   pLogs / "charset-build.log", pIconv, vEnvironment)
        shutil.copy2(pIconv / "libcharset/include/localcharset.h", pIconv / "lib/localcharset.h")
        runCommand([oArgs.strShell, "-c", shlex.quote(Path(oArgs.strMake).as_posix()) +
                    " -C lib -j" + str(oArgs.nJobs)],
                   pLogs / "iconv-build.log", pIconv, vEnvironment)
        (pPrefix / "lib").mkdir(parents=True, exist_ok=True)
        (pPrefix / "include").mkdir(parents=True, exist_ok=True)
        for pSource, strTargetName in (
                (pIconv / "lib/.libs/libiconv.a", "lib/libiconv.a"),
                (pIconv / "libcharset/lib/.libs/libcharset.a", "lib/libcharset.a"),
                (pIconv / "include/iconv.h.inst", "include/iconv.h"),
                (pIconv / "libcharset/include/libcharset.h.inst", "include/libcharset.h"),
                (pIconv / "libcharset/include/localcharset.h.inst", "include/localcharset.h")):
            shutil.copy2(pSource, pPrefix / strTargetName)
        for strLibrary in ("libcurl.a", "libmbedtls.a", "libmbedx509.a", "libmbedcrypto.a",
                           "libiconv.a", "libcharset.a"):
            verifyArchive(pPrefix / "lib" / strLibrary, nMachine)
        print("\nVerified " + strAbi + " libraries. Application CMake arguments:")
        for strArgument in (
                "-DCURL_DIR=" + (pPrefix / "lib/cmake/CURL").as_posix(),
                "-DIconv_INCLUDE_DIR=" + (pPrefix / "include").as_posix(),
                "-DIconv_LIBRARY=" + (pPrefix / "lib/libiconv.a").as_posix()):
            print(strArgument)
        print("WENKU8_TARGET_DEPS=" + pPrefix.as_posix())


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired,
            tarfile.TarError, zipfile.BadZipFile) as oError:
        print("Build failed: " + str(oError), file=sys.stderr)
        sys.exit(1)

