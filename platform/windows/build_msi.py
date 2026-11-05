# SPDX-License-Identifier: GPL-2.0-only
"""Build a per-user x64 or x86 MSI from a clean windeployqt staging directory."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import struct
import uuid
import xml.etree.ElementTree as ET


strNamespace = "http://schemas.microsoft.com/wix/2006/wi"
oComponentNamespace = uuid.UUID("49ea4f68-d98f-4a70-97c3-caa17edfdf4f")
ET.register_namespace("", strNamespace)


def addElement(oParent, strName, **vAttributes):
    return ET.SubElement(
        oParent,
        "{" + strNamespace + "}" + strName,
        {strKey: str(oValue) for strKey, oValue in vAttributes.items()},
    )


def identifier(strPrefix, strPath):
    return strPrefix + hashlib.sha256(str(strPath).encode()).hexdigest()[:24]


def componentGuid(strPath, strArchitecture="x64"):
    strKey = str(strPath) if strArchitecture == "x64" else strArchitecture + ":" + str(strPath)
    return str(uuid.uuid5(oComponentNamespace, strKey)).upper()


def validateStage(pStage, strArchitecture):
    nExpectedMachine = 0x8664 if strArchitecture == "x64" else 0x014C
    for pFile in pStage.rglob("*"):
        if not pFile.is_file() or pFile.suffix.lower() not in (".exe", ".dll"):
            continue
        with pFile.open("rb") as oStream:
            baHeader = oStream.read(64)
            if baHeader[:2] != b"MZ" or len(baHeader) != 64:
                raise ValueError(f"Invalid PE runtime: {pFile}")
            oStream.seek(struct.unpack_from("<I", baHeader, 60)[0])
            baSignature = oStream.read(6)
        if baSignature[:4] != b"PE\x00\x00" or struct.unpack_from("<H", baSignature, 4)[0] != nExpectedMachine:
            raise ValueError(f"Runtime does not match {strArchitecture}: {pFile}")


def addRuntimeFiles(oApplicationDirectory, pStage, strVersion, vComponents, strArchitecture):
    strWin64 = "yes" if strArchitecture == "x64" else "no"
    strRegistryKey = "Software\\Wenku8Cpp\\Wenku8ReaderSetup"
    if strArchitecture == "x86":
        strRegistryKey += "\\x86"
    vDirectories = {"": oApplicationDirectory}
    vFiles = [
        pFile
        for pFile in sorted(pStage.rglob("*"))
        if pFile.is_file()
        and pFile.suffix.lower() != ".pdb"
        and pFile.name != "opengl32sw.dll"
    ]
    for pFile in vFiles:
        pRelative = pFile.relative_to(pStage)
        strParent = ""
        for strPart in pRelative.parts[:-1]:
            strPath = strParent + "/" + strPart if strParent else strPart
            if strPath not in vDirectories:
                vDirectories[strPath] = addElement(
                    vDirectories[strParent], "Directory", Id=identifier("D", strPath), Name=strPart
                )
            strParent = strPath
        strKey = pRelative.as_posix()
        strComponentId = identifier("C", strKey)
        vComponents.append(strComponentId)
        oComponent = addElement(
            vDirectories[strParent], "Component", Id=strComponentId,
            Guid=componentGuid(strKey, strArchitecture), Win64=strWin64
        )
        addElement(
            oComponent, "File", Id=identifier("F", strKey), Source=str(pFile),
            Name=pFile.name, KeyPath="no", Checksum="yes"
        )
        addElement(
            oComponent, "RegistryValue", Root="HKCU",
            Key=strRegistryKey + "\\Files", Name=strComponentId,
            Type="string", Value=strVersion, KeyPath="yes"
        )
    for strPath, oDirectory in vDirectories.items():
        strComponentId = identifier("R", strPath)
        vComponents.append(strComponentId)
        oComponent = addElement(
            oDirectory, "Component", Id=strComponentId,
            Guid=componentGuid("directory:" + strPath, strArchitecture), Win64=strWin64
        )
        addElement(oComponent, "RemoveFolder", Id=identifier("Remove", strPath), On="uninstall")
        addElement(
            oComponent, "RegistryValue", Root="HKCU",
            Key=strRegistryKey + "\\Directories", Name=strComponentId,
            Type="integer", Value="1", KeyPath="yes"
        )


def buildInstaller(pStage, pWix, pOutput, strVersion, strArchitecture="x64"):
    strWin64 = "yes" if strArchitecture == "x64" else "no"
    strRegistryKey = "Software\\Wenku8Cpp\\Wenku8ReaderSetup"
    strApplicationDirectory = "Wenku8Reader"
    strShortcutName = "轻小说文库"
    strUpgradeCode = "A6C106AB-D48D-4BDB-91B6-534D0446A07C"
    if strArchitecture == "x86":
        strRegistryKey += "\\x86"
        strApplicationDirectory += "-x86"
        strShortcutName += " (32 位)"
        strUpgradeCode = "58388083-A799-563D-8368-D42E2032C22A"
    pStage = pStage.resolve()
    pOutput = pOutput.resolve()
    pOutput.mkdir(parents=True, exist_ok=True)
    validateStage(pStage, strArchitecture)
    oRoot = ET.Element("{" + strNamespace + "}Wix")
    oProduct = addElement(
        oRoot, "Product", Id="*", Name="轻小说文库", Language="2052", Codepage="936",
        Version=strVersion, Manufacturer="lucky_ji",
        UpgradeCode=strUpgradeCode
    )
    # Summary metadata has a separate code page from the installer database.
    addElement(
        oProduct, "Package", InstallerVersion="500", SummaryCodepage="936", Compressed="yes",
        InstallScope="perUser", InstallPrivileges="limited", Platform=strArchitecture,
        Description="轻小说文库 Windows " + strArchitecture
    )
    addElement(oProduct, "MajorUpgrade", DowngradeErrorMessage="已安装更新版本的轻小说文库。")
    oInstallProperty = addElement(oProduct, "Property", Id="INSTALLFOLDER", Secure="yes")
    addElement(
        oInstallProperty, "RegistrySearch", Id="FindExistingInstallDirectory", Root="HKCU",
        Key=strRegistryKey, Name="InstallDirectory", Type="raw", Win64=strWin64
    )
    addElement(oProduct, "MediaTemplate", EmbedCab="yes", CompressionLevel="high")
    if strArchitecture == "x64":
        addElement(oProduct, "Condition", Message="64-bit Windows is required.").text = "Installed OR VersionNT64"
    oTarget = addElement(oProduct, "Directory", Id="TARGETDIR", Name="SourceDir")
    oLocal = addElement(oTarget, "Directory", Id="LocalAppDataFolder")
    oPrograms = addElement(oLocal, "Directory", Id="UserPrograms", Name="Programs")
    oApplication = addElement(oPrograms, "Directory", Id="INSTALLFOLDER", Name=strApplicationDirectory)
    oMenu = addElement(oTarget, "Directory", Id="ProgramMenuFolder")
    oMenu = addElement(oMenu, "Directory", Id="AppMenu", Name=strShortcutName)
    addElement(oTarget, "Directory", Id="DesktopFolder")
    vComponents = ["InstallDirectoryRegistration", "UserProgramsDirectory"]
    oInstallComponent = addElement(
        oApplication, "Component", Id="InstallDirectoryRegistration",
        Guid=componentGuid("install-directory-registration", strArchitecture), Win64=strWin64
    )
    addElement(
        oInstallComponent, "RegistryValue", Root="HKCU", Key=strRegistryKey,
        Name="InstallDirectory", Type="string", Value="[INSTALLFOLDER]", KeyPath="yes"
    )
    oProgramsComponent = addElement(
        oPrograms, "Component", Id="UserProgramsDirectory",
        Guid=componentGuid("user-programs-directory", strArchitecture), Win64=strWin64
    )
    addElement(oProgramsComponent, "RemoveFolder", Id="RemoveEmptyUserPrograms", On="uninstall")
    addElement(
        oProgramsComponent, "RegistryValue", Root="HKCU", Key=strRegistryKey,
        Name="ProgramsDirectory", Type="integer", Value="1", KeyPath="yes"
    )
    addRuntimeFiles(oApplication, pStage, strVersion, vComponents, strArchitecture)
    addElement(oProduct, "Icon", Id="AppIcon", SourceFile=str(pStage / "app.ico"))
    addElement(oProduct, "Property", Id="ARPPRODUCTICON", Value="AppIcon")
    oShortcuts = addElement(oMenu, "Component", Id="Shortcuts", Guid=componentGuid("shortcuts", strArchitecture), Win64=strWin64)
    vComponents.append("Shortcuts")
    addElement(
        oShortcuts, "Shortcut", Id="StartShortcut", Name=strShortcutName,
        Target="[INSTALLFOLDER]wenku8_client.exe", WorkingDirectory="INSTALLFOLDER", Icon="AppIcon"
    )
    addElement(
        oShortcuts, "Shortcut", Id="DesktopShortcut", Directory="DesktopFolder", Name=strShortcutName,
        Target="[INSTALLFOLDER]wenku8_client.exe", WorkingDirectory="INSTALLFOLDER", Icon="AppIcon"
    )
    addElement(
        oShortcuts, "Shortcut", Id="LicenseShortcut", Name="开源许可与源码信息",
        Target="[INSTALLFOLDER]NOTICES.txt", WorkingDirectory="INSTALLFOLDER"
    )
    addElement(oShortcuts, "RemoveFolder", Id="RemoveAppMenu", On="uninstall")
    addElement(
        oShortcuts, "RegistryValue", Root="HKCU", Key=strRegistryKey,
        Name="Shortcuts", Type="integer", Value="1", KeyPath="yes"
    )
    oFeature = addElement(oProduct, "Feature", Id="Complete", Title="轻小说文库", Level="1")
    for strComponent in vComponents:
        addElement(oFeature, "ComponentRef", Id=strComponent)
    addElement(oProduct, "Property", Id="WIXUI_INSTALLDIR", Value="INSTALLFOLDER")
    addElement(oProduct, "WixVariable", Id="WixUILicenseRtf", Value=str(pOutput / "license.rtf"))
    addElement(oProduct, "UIRef", Id="WixUI_InstallDir")
    addElement(oProduct, "UIRef", Id="WixUI_ErrorProgressText")
    strLicense = (pStage / "licenses/REFERENCE-GPL-2.0.txt").read_text(encoding="utf-8-sig")
    strEscaped = strLicense.replace("\\", "\\\\").replace("{", "\\{").replace("}", "\\}").replace("\n", "\\par\n")
    (pOutput / "license.rtf").write_text(
        "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}\\fs18 " + strEscaped + "}",
        encoding="ascii", errors="replace"
    )
    pSource = pOutput / "Wenku8Reader.wxs"
    pObject = pOutput / "Wenku8Reader.wixobj"
    ET.ElementTree(oRoot).write(pSource, encoding="utf-8", xml_declaration=True)
    subprocess.run(
        [str(pWix / "candle.exe"), "-nologo", "-arch", strArchitecture, "-out", str(pObject), str(pSource)],
        check=True
    )
    pInstaller = pOutput / f"轻小说文库-{strVersion}-windows-{strArchitecture}.msi"
    # ICE91 describes per-machine redirection; this package is explicitly per-user.
    subprocess.run(
        [str(pWix / "light.exe"), "-nologo", "-sice:ICE91", "-ext", str(pWix / "WixUIExtension.dll"),
         "-cultures:zh-cn", "-out", str(pInstaller), str(pObject)],
        check=True
    )
    print(f"MSI: {pInstaller} ({pInstaller.stat().st_size} bytes)", flush=True)


def parseArguments():
    oParser = argparse.ArgumentParser(description=__doc__)
    oParser.add_argument("--stage", type=Path, required=True)
    oParser.add_argument("--wix", type=Path, required=True)
    oParser.add_argument("--output", type=Path, required=True)
    oParser.add_argument("--version", default="1.0.0")
    oParser.add_argument("--arch", choices=("x64", "x86"), default="x64")
    return oParser.parse_args()


if __name__ == "__main__":
    oArguments = parseArguments()
    buildInstaller(oArguments.stage, oArguments.wix, oArguments.output, oArguments.version, oArguments.arch)
