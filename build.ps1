# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Guidjlk
param(
    [Parameter(Mandatory = $true)][string]$SdkRoot,
    [string]$BuildDirectory = 'build',
    [string]$CMake = 'cmake',
    [string]$Ninja = 'ninja'
)
$ErrorActionPreference = 'Stop'
$r2Sdk = (Resolve-Path -LiteralPath $SdkRoot).Path.Replace('\', '/')
$r2Toolchain = "$r2Sdk/cmake/ps3-ppu-toolchain.cmake"
if (-not (Test-Path -LiteralPath $r2Toolchain)) {
    throw 'PS3DK toolchain file was not found in the supplied SDK directory.'
}
$env:PS3DK = $r2Sdk
$env:PS3DEV = $r2Sdk
$env:PSL1GHT = $r2Sdk
$r2Ninja = (Get-Command $Ninja -ErrorAction Stop).Source
& $CMake -S $PSScriptRoot -B $BuildDirectory -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$r2Toolchain" "-DCMAKE_MAKE_PROGRAM=$r2Ninja"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $CMake --build $BuildDirectory
if ($LASTEXITCODE -ne 0) { throw 'Compilation or packaging failed.' }
