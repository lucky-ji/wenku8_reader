# SPDX-License-Identifier: GPL-2.0-only
"""Record the exact native source tree and dependency provenance for release assembly."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    oParser = argparse.ArgumentParser(description=__doc__)
    oParser.add_argument("--metadata", type=Path, required=True)
    oParser.add_argument("--output", type=Path, required=True)
    oParser.add_argument("--system-qt", action="store_true")
    oArguments = oParser.parse_args()
    pRoot = Path(__file__).resolve().parents[2]
    pOutput = oArguments.output.resolve()
    pOutput.mkdir(parents=True, exist_ok=True)
    vMetadata = json.loads(oArguments.metadata.read_text(encoding="utf-8"))
    subprocess.run(["git", "archive", "--format=tar.gz", "--output=" + str(pOutput / "project.tar.gz"), "HEAD"], cwd=pRoot, check=True)
    shutil.copy2(oArguments.metadata, pOutput / "BUILD-METADATA.json")
    shutil.copytree(pRoot / "licenses", pOutput / "licenses")
    if vMetadata["platform"] == "linux":
        vSystemSources = {}
        for strPackage in vMetadata["systemRuntimePackages"]:
            strSource = subprocess.check_output(["dpkg-query", "-W", "-f=${source:Package}\t${source:Version}", strPackage], text=True).strip()
            vSystemSources[strPackage] = strSource
        (pOutput / "UBUNTU-SOURCE-VERSIONS.json").write_text(json.dumps(vSystemSources, indent=2) + "\n", encoding="utf-8")
        for strPackage in vMetadata["systemRuntimePackages"]:
            pCopyright = Path("/usr/share/doc") / strPackage.split(":")[0] / "copyright"
            if pCopyright.is_file():
                pDestination = pOutput / "licenses/system" / (strPackage.replace(":", "-") + ".copyright")
                pDestination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(pCopyright, pDestination)
    strQtProvenance = (
        "Qt is provided by Ubuntu, is not redistributed in this binary, and is recorded by exact package/source version in metadata.\n"
        if oArguments.system_qt else
        "Qt official 5.15.2 prebuilt kit. Match the unmodified Qt 5.15.2 source modules when assembling the release source package.\n"
        "Upstream complete source: https://download.qt.io/archive/qt/5.15/5.15.2/single/qt-everywhere-src-5.15.2.tar.xz\n"
        "SHA-256: 3a530d1b243b5dec00bc54937455471aaa3e56849d2593edb8ded07228202240\n"
    )
    (pOutput / "SOURCE-PROVENANCE.txt").write_text(
        "Light Novel Library 1.0.0 native build provenance\n\n" + strQtProvenance
        + "Project commit and tree hashes are recorded in BUILD-METADATA.json; project.tar.gz is the exact git tree.\n"
        "Bundled curl, libxml2 and TinyXML-2 sources are inside project.tar.gz.\n"
        "Linux TLS uses OS-provided Mbed TLS (GPL-2.0-or-later option), not OpenSSL. OS libraries are not bundled.\n"
        "macOS TLS uses the system SecureTransport framework.\n"
        "For official Qt kits, release assembly must add matching Qt sources and all original notices before publishing.\n"
        "This artifact records provenance; it does not claim to be a complete Qt source distribution by itself.\n",
        encoding="utf-8",
    )
    vHashes = [hashlib.sha256(pFile.read_bytes()).hexdigest() + "  " + pFile.relative_to(pOutput).as_posix() for pFile in sorted(pOutput.rglob("*")) if pFile.is_file()]
    (pOutput / "SHA256SUMS.txt").write_text("\n".join(vHashes) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
