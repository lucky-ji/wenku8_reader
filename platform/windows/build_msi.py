# SPDX-License-Identifier: GPL-2.0-only
"""Build a per-user x64 MSI from a clean windeployqt staging directory."""
import argparse, hashlib, subprocess, uuid
from pathlib import Path
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser();p.add_argument('--stage',type=Path,required=True);p.add_argument('--wix',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--version',default='1.0.0');args=p.parse_args()
stage=args.stage.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
NS='http://schemas.microsoft.com/wix/2006/wi';ET.register_namespace('',NS)
def element(parent,name,**attributes):return ET.SubElement(parent,'{'+NS+'}'+name,{k:str(v) for k,v in attributes.items()})
def ident(prefix,path):return prefix+hashlib.sha256(str(path).encode()).hexdigest()[:24]
def guid(path):return str(uuid.uuid5(uuid.UUID('49ea4f68-d98f-4a70-97c3-caa17edfdf4f'),str(path))).upper()
root=ET.Element('{'+NS+'}Wix')
product=element(root,'Product',Id='*',Name='轻小说文库',Language='2052',Codepage='936',Version=args.version,Manufacturer='lucky_ji',UpgradeCode='A6C106AB-D48D-4BDB-91B6-534D0446A07C')
element(product,'Package',InstallerVersion='500',Compressed='yes',InstallScope='perUser',InstallPrivileges='limited',Platform='x64',Description='轻小说文库 Windows x64')
element(product,'MajorUpgrade',DowngradeErrorMessage='已安装更新版本的轻小说文库。')
install_property=element(product,'Property',Id='INSTALLFOLDER',Secure='yes')
element(install_property,'RegistrySearch',Id='FindExistingInstallDirectory',Root='HKCU',Key='Software\\Wenku8Cpp\\Wenku8ReaderSetup',Name='InstallDirectory',Type='raw',Win64='yes')
element(product,'MediaTemplate',EmbedCab='yes',CompressionLevel='high')
element(product,'Condition',Message='64-bit Windows is required.').text='Installed OR VersionNT64'
target=element(product,'Directory',Id='TARGETDIR',Name='SourceDir')
local=element(target,'Directory',Id='LocalAppDataFolder')
programs=element(local,'Directory',Id='UserPrograms',Name='Programs')
app=element(programs,'Directory',Id='INSTALLFOLDER',Name='Wenku8Reader')
menu=element(target,'Directory',Id='ProgramMenuFolder');menu=element(menu,'Directory',Id='AppMenu',Name='轻小说文库')
element(target,'Directory',Id='DesktopFolder')
directories={'':app};components=[]
install_component=element(app,'Component',Id='InstallDirectoryRegistration',Guid=guid('install-directory-registration'),Win64='yes');components.append('InstallDirectoryRegistration')
element(install_component,'RegistryValue',Root='HKCU',Key='Software\\Wenku8Cpp\\Wenku8ReaderSetup',Name='InstallDirectory',Type='string',Value='[INSTALLFOLDER]',KeyPath='yes')
programs_component=element(programs,'Component',Id='UserProgramsDirectory',Guid=guid('user-programs-directory'),Win64='yes');components.append('UserProgramsDirectory')
element(programs_component,'RemoveFolder',Id='RemoveEmptyUserPrograms',On='uninstall')
element(programs_component,'RegistryValue',Root='HKCU',Key='Software\\Wenku8Cpp\\Wenku8ReaderSetup',Name='ProgramsDirectory',Type='integer',Value='1',KeyPath='yes')
files=[f for f in sorted(stage.rglob('*')) if f.is_file() and f.suffix.lower()!='.pdb' and f.name!='opengl32sw.dll']
for file in files:
    rel=file.relative_to(stage);parent=''
    for part in rel.parts[:-1]:
        path=parent+'/'+part if parent else part
        if path not in directories:directories[path]=element(directories[parent],'Directory',Id=ident('D',path),Name=part)
        parent=path
    key=rel.as_posix();cid=ident('C',key);components.append(cid)
    component=element(directories[parent],'Component',Id=cid,Guid=guid(key),Win64='yes')
    element(component,'File',Id=ident('F',key),Source=str(file),Name=file.name,KeyPath='no',Checksum='yes')
    element(component,'RegistryValue',Root='HKCU',Key='Software\\Wenku8Cpp\\Wenku8ReaderSetup\\Files',Name=cid,Type='string',Value=args.version,KeyPath='yes')
for path,directory in directories.items():
    cid=ident('R',path);components.append(cid);component=element(directory,'Component',Id=cid,Guid=guid('directory:'+path),Win64='yes')
    element(component,'RemoveFolder',Id=ident('Remove',path),On='uninstall')
    element(component,'RegistryValue',Root='HKCU',Key='Software\\Wenku8Cpp\\Wenku8ReaderSetup\\Directories',Name=cid,Type='integer',Value='1',KeyPath='yes')
element(product,'Icon',Id='AppIcon',SourceFile=str(stage/'app.ico'))
element(product,'Property',Id='ARPPRODUCTICON',Value='AppIcon')
shortcuts=element(menu,'Component',Id='Shortcuts',Guid=guid('shortcuts'),Win64='yes');components.append('Shortcuts')
element(shortcuts,'Shortcut',Id='StartShortcut',Name='轻小说文库',Target='[INSTALLFOLDER]wenku8_client.exe',WorkingDirectory='INSTALLFOLDER',Icon='AppIcon')
element(shortcuts,'Shortcut',Id='DesktopShortcut',Directory='DesktopFolder',Name='轻小说文库',Target='[INSTALLFOLDER]wenku8_client.exe',WorkingDirectory='INSTALLFOLDER',Icon='AppIcon')
element(shortcuts,'Shortcut',Id='LicenseShortcut',Name='开源许可与源码信息',Target='[INSTALLFOLDER]NOTICES.txt',WorkingDirectory='INSTALLFOLDER')
element(shortcuts,'RemoveFolder',Id='RemoveAppMenu',On='uninstall')
element(shortcuts,'RegistryValue',Root='HKCU',Key='Software\\Wenku8Cpp\\Wenku8ReaderSetup',Name='Shortcuts',Type='integer',Value='1',KeyPath='yes')
feature=element(product,'Feature',Id='Complete',Title='轻小说文库',Level='1')
for component in components:element(feature,'ComponentRef',Id=component)
element(product,'Property',Id='WIXUI_INSTALLDIR',Value='INSTALLFOLDER')
element(product,'WixVariable',Id='WixUILicenseRtf',Value=str(out/'license.rtf'))
element(product,'UIRef',Id='WixUI_InstallDir');element(product,'UIRef',Id='WixUI_ErrorProgressText')
license_text=(stage/'licenses/REFERENCE-GPL-2.0.txt').read_text(encoding='utf-8-sig')
escaped=license_text.replace('\\','\\\\').replace('{','\\{').replace('}','\\}').replace('\n','\\par\n')
(out/'license.rtf').write_text('{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}\\fs18 '+escaped+'}',encoding='ascii',errors='replace')
source=out/'Wenku8Reader.wxs';ET.ElementTree(root).write(source,encoding='utf-8',xml_declaration=True)
subprocess.run([str(args.wix/'candle.exe'),'-nologo','-arch','x64','-out',str(out/'Wenku8Reader.wixobj'),str(source)],check=True)
installer=out/f'轻小说文库-{args.version}-windows-x64.msi'
# ICE91 describes per-machine redirection; this package is explicitly per-user.
subprocess.run([str(args.wix/'light.exe'),'-nologo','-sice:ICE91','-ext',str(args.wix/'WixUIExtension.dll'),'-cultures:zh-cn','-out',str(installer),str(out/'Wenku8Reader.wixobj')],check=True)
print(f'MSI: {installer} ({installer.stat().st_size} bytes)',flush=True)
