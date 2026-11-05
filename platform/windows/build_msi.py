# SPDX-License-Identifier: GPL-2.0-only
"""Build a per-user x64 MSI from a clean windeployqt staging directory."""
import argparse
import hashlib
from pathlib import Path
import subprocess
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


def componentGuid(strPath):
    return str(uuid.uuid5(oComponentNamespace, str(strPath))).upper()


def addRuntimeFiles(oApplicationDirectory, pStage, strVersion, vComponents):
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
            Guid=componentGuid(strKey), Win64="yes"
        )
        addElement(
            oComponent, "File", Id=identifier("F", strKey), Source=str(pFile),
            Name=pFile.name, KeyPath="no", Checksum="yes"
        )
        addElement(
            oComponent, "RegistryValue", Root="HKCU",
            Key="Software\\Wenku8Cpp\\Wenku8ReaderSetup\\Files", Name=strComponentId,
            Type="string", Value=strVersion, KeyPath="yes"
        )
    for strPath, oDirectory in vDirectories.items():
        strComponentId = identifier("R", strPath)
        vComponents.append(strComponentId)
        oComponent = addElement(
            oDirectory, "Component", Id=strComponentId,
            Guid=componentGuid("directory:" + strPath), Win64="yes"
        )
        addElement(oComponent, "RemoveFolder", Id=identifier("Remove", strPath), On="uninstall")
        addElement(
            oComponent, "RegistryValue", Root="HKCU",
            Key="Software\\Wenku8Cpp\\Wenku8ReaderSetup\\Directories", Name=strComponentId,
            Type="integer", Value="1", KeyPath="yes"
        )


def buildInstaller(pStage, pWix, pOutput, strVersion):
    pStage = pStage.resolve()
    pOutput = pOutput.resolve()
    pOutput.mkdir(parents=True, exist_ok=True)
    oRoot = ET.Element("{" + strNamespace + "}Wix")
    oProduct = addElement(
        oRoot, "Product", Id="*", Name="轻小说文库", Language="2052", Codepage="936",
        Version=strVersion, Manufacturer="lucky_ji",
        UpgradeCode="A6C106AB-D48D-4BDB-91B6-534D0446A07C"
    )
    # Summary metadata has a separate code page from the installer database.
    addElement(
        oProduct, "Package", InstallerVersion="500", SummaryCodepage="936", Compressed="yes",
        InstallScope="perUser", InstallPrivileges="limited", Platform="x64",
        Description="轻小说文库 Windows x64"
    )
    addElement(oProduct, "MajorUpgrade", DowngradeErrorMessage="已安装更新版本的轻小说文库。")
    oInstallProperty = addElement(oProduct, "Property", Id="INSTALLFOLDER", Secure="yes")
    addElement(
        oInstallProperty, "RegistrySearch", Id="FindExistingInstallDirectory", Root="HKCU",
        Key="Software\\Wenku8Cpp\\Wenku8ReaderSetup", Name="InstallDirectory", Type="raw", Win64="yes"
    )
    addElement(oProduct, "MediaTemplate", EmbedCab="yes", CompressionLevel="high")
    addElement(oProduct, "Condition", Message="64-bit Windows is required.").text = "Installed OR VersionNT64"
    oTarget = addElement(oProduct, "Directory", Id="TARGETDIR", Name="SourceDir")
    oLocal = addElement(oTarget, "Directory", Id="LocalAppDataFolder")
    oPrograms = addElement(oLocal, "Directory", Id="UserPrograms", Name="Programs")
    oApplication = addElement(oPrograms, "Directory", Id="INSTALLFOLDER", Name="Wenku8Reader")
    oMenu = addElement(oTarget, "Directory", Id="ProgramMenuFolder")
    oMenu = addElement(oMenu, "Directory", Id="AppMenu", Name="轻小说文库")
    addElement(oTarget, "Directory", Id="DesktopFolder")
    vComponents = ["InstallDirectoryRegistration", "UserProgramsDirectory"]
    oInstallComponent = addElement(
        oApplication, "Component", Id="InstallDirectoryRegistration",
        Guid=componentGuid("install-directory-registration"), Win64="yes"
    )
    addElement(
        oInstallComponent, "RegistryValue", Root="HKCU", Key="Software\\Wenku8Cpp\\Wenku8ReaderSetup",
        Name="InstallDirectory", Type="string", Value="[INSTALLFOLDER]", KeyPath="yes"
    )
    oProgramsComponent = addElement(
        oPrograms, "Component", Id="UserProgramsDirectory",
        Guid=componentGuid("user-programs-directory"), Win64="yes"
    )
    addElement(oProgramsComponent, "RemoveFolder", Id="RemoveEmptyUserPrograms", On="uninstall")
    addElement(
        oProgramsComponent, "RegistryValue", Root="HKCU", Key="Software\\Wenku8Cpp\\Wenku8ReaderSetup",
        Name="ProgramsDirectory", Type="integer", Value="1", KeyPath="yes"
    )
    addRuntimeFiles(oApplication, pStage, strVersion, vComponents)
    addElement(oProduct, "Icon", Id="AppIcon", SourceFile=str(pStage / "app.ico"))
    addElement(oProduct, "Property", Id="ARPPRODUCTICON", Value="AppIcon")
    oShortcuts = addElement(oMenu, "Component", Id="Shortcuts", Guid=componentGuid("shortcuts"), Win64="yes")
    vComponents.append("Shortcuts")
    addElement(
        oShortcuts, "Shortcut", Id="StartShortcut", Name="轻小说文库",
        Target="[INSTALLFOLDER]wenku8_client.exe", WorkingDirectory="INSTALLFOLDER", Icon="AppIcon"
    )
    addElement(
        oShortcuts, "Shortcut", Id="DesktopShortcut", Directory="DesktopFolder", Name="轻小说文库",
        Target="[INSTALLFOLDER]wenku8_client.exe", WorkingDirectory="INSTALLFOLDER", Icon="AppIcon"
    )
    addElement(
        oShortcuts, "Shortcut", Id="LicenseShortcut", Name="开源许可与源码信息",
        Target="[INSTALLFOLDER]NOTICES.txt", WorkingDirectory="INSTALLFOLDER"
    )
    addElement(oShortcuts, "RemoveFolder", Id="RemoveAppMenu", On="uninstall")
    addElement(
        oShortcuts, "RegistryValue", Root="HKCU", Key="Software\\Wenku8Cpp\\Wenku8ReaderSetup",
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
        [str(pWix / "candle.exe"), "-nologo", "-arch", "x64", "-out", str(pObject), str(pSource)],
        check=True
    )
    pInstaller = pOutput / f"轻小说文库-{strVersion}-windows-x64.msi"
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
    return oParser.parse_args()


if __name__ == "__main__":
    oArguments = parseArguments()
    buildInstaller(oArguments.stage, oArguments.wix, oArguments.output, oArguments.version)
