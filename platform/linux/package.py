# SPDX-License-Identifier: GPL-2.0-only
"""Package a native Linux Qt 5 client, retaining OS libraries as runtime dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile


def command(vArguments, **vOptions):
    return subprocess.check_output(vArguments, text=True, **vOptions).strip()


def main():
    oParser = argparse.ArgumentParser(description=__doc__)
    oParser.add_argument("--build", type=Path, required=True)
    oParser.add_argument("--output", type=Path, required=True)
    oParser.add_argument("--qt-root", type=Path, required=True)
    oParser.add_argument("--system-qt", action="store_true")
    oArguments = oParser.parse_args()
    pRoot = Path(__file__).resolve().parents[2]
    strArchitecture = command(["uname", "-m"])
    pOutput = oArguments.output.resolve()
    pOutput.mkdir(parents=True, exist_ok=True)
    pBundle = pOutput / ("wenku8-reader-1.0.0-linux-" + strArchitecture)
    pBundle.mkdir()
    for strDirectory in ("bin", "lib", "libexec", "plugins", "resources", "translations", "licenses/system"):
        (pBundle / strDirectory).mkdir(parents=True, exist_ok=True)
    pBinary = pBundle / "bin/wenku8_client"
    shutil.copy2(oArguments.build / "app/wenku8_client", pBinary)
    shutil.copytree(pRoot / "licenses", pBundle / "licenses", dirs_exist_ok=True)
    shutil.copy2(pRoot / "LICENSE", pBundle / "LICENSE")
    shutil.copy2(pRoot / "THIRD_PARTY_NOTICES.md", pBundle / "THIRD_PARTY_NOTICES.md")
    shutil.copy2(pRoot / "app/resources/app-icon.png", pBundle / "app-icon.png")
    pQt = oArguments.qt_root.resolve()
    pQtLibrary = pQt / "lib"
    vEnvironment = dict(os.environ)
    vEnvironment["LD_LIBRARY_PATH"] = str(pQtLibrary) + ":" + vEnvironment.get("LD_LIBRARY_PATH", "")
    vPending = [pBinary]
    if not oArguments.system_qt:
        pHelper = pQt / "libexec/QtWebEngineProcess"
        if not pHelper.is_file():
            raise RuntimeError("Real Qt 5 WebEngine helper is missing")
        shutil.copy2(pHelper, pBundle / "libexec/QtWebEngineProcess")
        vPending.append(pHelper)
        for strGroup in ("platforms", "imageformats", "iconengines", "xcbglintegrations"):
            for pPlugin in (pQt / "plugins" / strGroup).glob("*.so"):
                pDestination = pBundle / "plugins" / strGroup / pPlugin.name
                pDestination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(pPlugin, pDestination)
                vPending.append(pPlugin)
        shutil.copytree(pQt / "resources", pBundle / "resources", dirs_exist_ok=True)
        for pTranslation in (pQt / "translations").glob("qtwebengine*"):
            pDestination = pBundle / "translations" / pTranslation.name
            if pTranslation.is_dir():
                shutil.copytree(pTranslation, pDestination)
            else:
                shutil.copy2(pTranslation, pDestination)
    vVisited = set()
    vSystemLibraries = set()
    while vPending:
        pLibrary = vPending.pop()
        if str(pLibrary) in vVisited:
            continue
        vVisited.add(str(pLibrary))
        strDependencies = command(["ldd", str(pLibrary)], env=vEnvironment)
        if "not found" in strDependencies:
            raise RuntimeError("Missing runtime dependency for " + str(pLibrary) + "\n" + strDependencies)
        for strLine in strDependencies.splitlines():
            oMatch = re.search(r"=> (/[^ ]+)", strLine)
            if not oMatch:
                continue
            pDependency = Path(oMatch.group(1))
            if not oArguments.system_qt and pDependency.resolve().is_relative_to(pQt):
                pDestination = pBundle / "lib" / pDependency.name
                if not pDestination.exists():
                    shutil.copy2(pDependency.resolve(), pDestination)
                    vPending.append(pDependency)
            else:
                vSystemLibraries.add(str(pDependency))
    vPackages = {}
    for strLibrary in sorted(vSystemLibraries):
        try:
            strOwner = command(["dpkg-query", "-S", strLibrary]).split(": ", 1)[0]
        except subprocess.CalledProcessError:
            strOwner = command(["dpkg-query", "-S", str(Path(strLibrary).resolve())]).split(": ", 1)[0]
        strVersion = command(["dpkg-query", "-W", "-f=${Version}", strOwner])
        vPackages[strOwner] = strVersion
        pCopyright = Path("/usr/share/doc") / strOwner.split(":")[0] / "copyright"
        if pCopyright.is_file():
            shutil.copy2(pCopyright, pBundle / "licenses/system" / (strOwner.replace(":", "-") + ".copyright"))
    vMetadata = {
        "version": "1.0.0", "platform": "linux", "architecture": strArchitecture,
        "commit": command(["git", "rev-parse", "HEAD"], cwd=pRoot),
        "tree": command(["git", "rev-parse", "HEAD^{tree}"], cwd=pRoot),
        "qtVersion": command([str(pQt / "bin/qmake"), "-query", "QT_VERSION"]),
        "qtDistribution": "Ubuntu system Qt" if oArguments.system_qt else "Qt official 5.15.2 prebuilt",
        "systemRuntimePackages": vPackages,
        "bundledQtLibraries": sorted(pFile.name for pFile in (pBundle / "lib").glob("libQt5*")),
        "curlTlsBackend": "Mbed TLS (GPL-2.0-or-later option)",
    }
    (pBundle / "BUILD-METADATA.json").write_text(json.dumps(vMetadata, indent=2) + "\n", encoding="utf-8")
    (pOutput / "BUILD-METADATA.json").write_text(json.dumps(vMetadata, indent=2) + "\n", encoding="utf-8")
    (pBundle / "RUNTIME-DEPENDENCIES.txt").write_text(
        "Built natively for Ubuntu " + ("24.04 ARM64" if oArguments.system_qt else "22.04 x86_64") + ".\n"
        "OS libraries are not bundled. Install matching Ubuntu runtime packages; newer compatible updates may be used.\n"
        + "\n".join(strName + "=" + strVersion for strName, strVersion in sorted(vPackages.items())) + "\n",
        encoding="utf-8",
    )
    strLauncher = '#!/bin/sh\nset -eu\nstrDirectory="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"\n'
    if not oArguments.system_qt:
        strLauncher += ('export LD_LIBRARY_PATH="$strDirectory/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
                        'export QT_PLUGIN_PATH="$strDirectory/plugins"\n'
                        'export QTWEBENGINEPROCESS_PATH="$strDirectory/libexec/QtWebEngineProcess"\n'
                        'export QTWEBENGINE_RESOURCES_PATH="$strDirectory/resources"\n'
                        'export QTWEBENGINE_LOCALES_PATH="$strDirectory/translations/qtwebengine_locales"\n')
    strLauncher += 'exec "$strDirectory/bin/wenku8_client" "$@"\n'
    pLauncher = pBundle / "wenku8_reader"
    pLauncher.write_text(strLauncher, encoding="utf-8")
    pLauncher.chmod(0o755)
    pArchive = pOutput / (pBundle.name + ".tar.gz")
    with tarfile.open(pArchive, "w:gz") as oArchive:
        oArchive.add(pBundle, arcname=pBundle.name)
    strHash = hashlib.sha256(pArchive.read_bytes()).hexdigest()
    (pOutput / "SHA256SUMS.txt").write_text(strHash + "  " + pArchive.name + "\n", encoding="utf-8")
    print(pArchive)


if __name__ == "__main__":
    main()
