# SPDX-License-Identifier: GPL-2.0-only
param(
    [Parameter(Mandatory = $true)][string]$Output,
    [string]$Source = "$PSScriptRoot/../../app/resources/app-icon.png"
)
Add-Type -AssemblyName System.Drawing
$oSource = [Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Source))
$vImages = @()
foreach ($nSize in @(16, 24, 32, 48, 64, 128, 256))
{
    $oBitmap = [Drawing.Bitmap]::new($nSize, $nSize)
    $oGraphics = [Drawing.Graphics]::FromImage($oBitmap)
    $oGraphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $oGraphics.DrawImage($oSource, 0, 0, $nSize, $nSize)
    $oStream = [IO.MemoryStream]::new()
    $oBitmap.Save($oStream, [Drawing.Imaging.ImageFormat]::Png)
    $vImages += ,@($nSize, $oStream.ToArray())
    $oStream.Dispose()
    $oGraphics.Dispose()
    $oBitmap.Dispose()
}
$oSource.Dispose()
$oFile = [IO.File]::Create($Output)
$oWriter = [IO.BinaryWriter]::new($oFile)
$oWriter.Write([uint16]0)
$oWriter.Write([uint16]1)
$oWriter.Write([uint16]$vImages.Count)
$nOffset = 6 + 16 * $vImages.Count
foreach ($vEntry in $vImages)
{
    $nSize = $vEntry[0]
    $vBytes = $vEntry[1]
    $nDimension = if ($nSize -eq 256) { 0 } else { $nSize }
    $oWriter.Write([byte]$nDimension)
    $oWriter.Write([byte]$nDimension)
    $oWriter.Write([byte]0)
    $oWriter.Write([byte]0)
    $oWriter.Write([uint16]1)
    $oWriter.Write([uint16]32)
    $oWriter.Write([uint32]$vBytes.Length)
    $oWriter.Write([uint32]$nOffset)
    $nOffset += $vBytes.Length
}
foreach ($vEntry in $vImages)
{
    $oWriter.Write([byte[]]$vEntry[1])
}
$oWriter.Dispose()
$oFile.Dispose()
