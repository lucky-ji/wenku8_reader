# SPDX-License-Identifier: GPL-2.0-only
[CmdletBinding()]
param(
    [string]$DevEcoStudio,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../../out/harmony')
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$strBuildProfile = Join-Path $PSScriptRoot 'build-profile.json5'
if ((Get-Content -LiteralPath $strBuildProfile -Raw) -match '"signingConfigs?"\s*:') {
    throw 'Remove local signing configuration from build-profile.json5 before building the unsigned release.'
}
if (-not $DevEcoStudio) {
    $DevEcoStudio = if ($env:DEVECO_SDK_HOME) {
        Split-Path -Parent $env:DEVECO_SDK_HOME
    } else {
        Join-Path $env:ProgramFiles 'Huawei/DevEco Studio'
    }
}

$strStudio = (Resolve-Path -LiteralPath $DevEcoStudio).Path
$strSdk = Join-Path $strStudio 'sdk'
$strJbr = Join-Path $strStudio 'jbr'
$strNodeHome = Join-Path $strStudio 'tools/node'
$strNode = Join-Path $strNodeHome 'node.exe'
$strHvigor = Join-Path $strStudio 'tools/hvigor/bin/hvigorw.js'
$strOhpm = Join-Path $strStudio 'tools/ohpm/bin/ohpm.bat'
$strJava = Join-Path $strJbr 'bin/java.exe'
$strSignTool = Join-Path $strSdk 'default/openharmony/toolchains/lib/hap-sign-tool.jar'
foreach ($strTool in @($strNode, $strHvigor, $strOhpm, $strJava, $strSignTool)) {
    if (-not (Test-Path -LiteralPath $strTool -PathType Leaf)) {
        throw "DevEco Studio tool not found: $strTool"
    }
}

$strExpectedHap = Join-Path $PSScriptRoot 'entry/build/default/outputs/default/entry-default-unsigned.hap'
$strRepository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$vNotices = @(
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
    'third_party/tinyxml2/LICENSE.txt',
    'licenses/curl-8.0.1.txt',
    'licenses/libxml2-2.15.4.txt',
    'licenses/mbedtls-3.6.5.txt',
    'licenses/llvm-libcxx.txt'
)
$strNotices = (New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'entry/src/main/resources/rawfile/licenses') -Force).FullName
foreach ($strNotice in $vNotices) {
    $strName = if ($strNotice -eq 'third_party/tinyxml2/LICENSE.txt') {
        'tinyxml2.txt'
    } else {
        Split-Path -Leaf $strNotice
    }
    Copy-Item -LiteralPath (Join-Path $strRepository $strNotice) -Destination (Join-Path $strNotices $strName) -Force
}
$strPreviousSdk = $env:DEVECO_SDK_HOME
$strPreviousJava = $env:JAVA_HOME
$strPreviousPath = $env:PATH
Push-Location -LiteralPath $PSScriptRoot
try {
    $env:DEVECO_SDK_HOME = $strSdk
    $env:JAVA_HOME = $strJbr
    $env:PATH = "$strNodeHome;$strJbr/bin;$strPreviousPath"

    & $strOhpm install --all
    if ($LASTEXITCODE -ne 0) {
        throw 'OHPM could not resolve the HarmonyOS application module.'
    }
    $oStarted = [DateTime]::UtcNow
    & $strNode $strHvigor --mode module -p product=default -p module=entry@default `
        -p buildMode=release assembleHap --no-daemon --no-incremental
    if ($LASTEXITCODE -ne 0) {
        throw 'Hvigor failed to build the release HAP.'
    }
    if (-not (Test-Path -LiteralPath $strExpectedHap -PathType Leaf) -or
        (Get-Item -LiteralPath $strExpectedHap).LastWriteTimeUtc -lt $oStarted) {
        throw 'No new unsigned release HAP was produced.'
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $oArchive = [System.IO.Compression.ZipFile]::OpenRead($strExpectedHap)
    try {
        $oManifest = $oArchive.GetEntry('module.json')
        if ($null -eq $oManifest -or $null -eq $oArchive.GetEntry('ets/modules.abc')) {
            throw 'HAP is missing its compiled manifest or ArkTS code.'
        }
        foreach ($oNotice in (Get-ChildItem -LiteralPath $strNotices -File)) {
            if ($null -eq $oArchive.GetEntry("resources/rawfile/licenses/$($oNotice.Name)")) {
                throw "HAP is missing a dependency notice: $($oNotice.Name)"
            }
        }
        $oReader = [IO.StreamReader]::new($oManifest.Open())
        try {
            $oMetadata = $oReader.ReadToEnd() | ConvertFrom-Json
        } finally {
            $oReader.Dispose()
        }
        $oConfig = (Get-Content -LiteralPath 'AppScope/app.json5' -Raw | ConvertFrom-Json).app
        if ($oMetadata.app.buildMode -ne 'release' -or $oMetadata.app.debug -ne $false -or
            $oMetadata.app.bundleName -ne $oConfig.bundleName -or
            $oMetadata.app.versionCode -ne $oConfig.versionCode -or
            $oMetadata.app.versionName -ne $oConfig.versionName -or
            $oMetadata.module.type -ne 'entry' -or
            $oMetadata.module.mainElement -ne 'EntryAbility' -or
            (Compare-Object @('2in1', 'phone', 'tablet') $oMetadata.module.deviceTypes)) {
            throw 'HAP release metadata does not match the project or supported devices.'
        }
        $oBridge = $oArchive.GetEntry('libs/arm64-v8a/libwenku8_bridge.so')
        if ($null -eq $oBridge) {
            throw 'HAP is missing the ARM64 C++ SDK bridge.'
        }
        $oBinary = [IO.BinaryReader]::new($oBridge.Open())
        try {
            $vHeader = $oBinary.ReadBytes(20)
            if ($vHeader.Length -ne 20 -or [BitConverter]::ToUInt32($vHeader, 0) -ne 0x464C457F -or
                $vHeader[4] -ne 2 -or $vHeader[5] -ne 1 -or
                [BitConverter]::ToUInt16($vHeader, 18) -ne 183) {
                throw 'The packaged SDK bridge is not an AArch64 ELF library.'
            }
        } finally {
            $oBinary.Dispose()
        }
    } finally {
        $oArchive.Dispose()
    }

    $strSigningProbe = Join-Path $env:TEMP ("wenku8-hap-" + [Guid]::NewGuid().ToString('N'))
    $oSigningCheck = & $strJava -jar $strSignTool verify-app -inFile $strExpectedHap `
        -outCertChain "$strSigningProbe.cer" -outProfile "$strSigningProbe.p7b" 2>&1
    if ($LASTEXITCODE -eq 0 -or -not ($oSigningCheck -match 'No Hap Signing Block')) {
        throw 'The HAP signing verifier could not confirm that the package is unsigned.'
    }
} finally {
    Pop-Location
    $env:DEVECO_SDK_HOME = $strPreviousSdk
    $env:JAVA_HOME = $strPreviousJava
    $env:PATH = $strPreviousPath
}

$strDestination = (New-Item -ItemType Directory -Path $OutputDirectory -Force).FullName
$strVersion = $oMetadata.app.versionName
$strHap = Join-Path $strDestination "wenku8-reader-$strVersion-harmony-arm64-v8a-release-unsigned.hap"
Copy-Item -LiteralPath $strExpectedHap -Destination $strHap -Force
Get-FileHash -LiteralPath $strHap -Algorithm SHA256
exit 0
