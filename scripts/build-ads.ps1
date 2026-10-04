<#
.SYNOPSIS
    Builds the Qt Advanced Docking System (ADS) for one Qt kit into this repository's build/ folder.

.DESCRIPTION
    MC Workbench (tools/mc_workbench, SPEC-gui-tool.md) links ADS as a DLL that is never part of the
    repository. The prebuilt package on this PC is for MSVC only; this script builds ADS from its
    source with the chosen Qt kit (typically MinGW), Debug and Release, and installs both into
    build/ads-<kit>/install, where <kit> is the folder name of the Qt kit (for example mingw_64).

    Nothing outside the project folder is written: the source folder is only read, the build trees
    are build/ads-<kit>/build-debug and build/ads-<kit>/build-release, the install prefix is
    build/ads-<kit>/install. Both configurations share one prefix, so a CMake or qmake consumer finds
    qtadvanceddocking-qt6d and qtadvanceddocking-qt6 there.

    At the end the script prints the MC_ADS_DIR to use:
      - CMake: -DMC_ADS_DIR=<install>  (or the environment variable MC_ADS_DIR)
      - qmake: MC_ADS_DIR (MSVC kits) or MC_ADS_DIR_MINGW (MinGW kits) in the git-ignored mc_local.pri

.PARAMETER QtDir
    The Qt kit to build for. Default C:/Qt/6.11.1/mingw_64. A kit folder whose name contains
    "mingw" is built with the MinGW compiler found under C:\Qt\Tools; any other kit with the
    Visual Studio developer shell (scripts/vsdev.ps1).

.PARAMETER SourceDir
    ADS source folder (CMake only), read only. Default: the environment variable MC_ADS_SOURCE_DIR,
    else C:/build_packages/Qt-Advanced-Docking-System.

.PARAMETER Jobs
    Parallel jobs. Default: the number of logical processors.

.PARAMETER Clean
    Delete build/ads-<kit> first (only that folder) and build from scratch.

.EXAMPLE
    scripts/build-ads.ps1
    scripts/build-ads.ps1 -QtDir C:/Qt/6.11.1/msvc2022_64
#>
[CmdletBinding()]
param(
    [string]$QtDir = 'C:/Qt/6.11.1/mingw_64',
    [string]$SourceDir = '',
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SourceDir) {
    if ($env:MC_ADS_SOURCE_DIR) { $SourceDir = $env:MC_ADS_SOURCE_DIR }
    else { $SourceDir = 'C:/build_packages/Qt-Advanced-Docking-System' }
}
if (-not (Test-Path -LiteralPath (Join-Path $SourceDir 'CMakeLists.txt'))) {
    throw "build-ads: no CMakeLists.txt in the ADS source folder '$SourceDir' (set -SourceDir or MC_ADS_SOURCE_DIR)."
}
if (-not (Test-Path -LiteralPath (Join-Path $QtDir 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw "build-ads: '$QtDir' is not a Qt 6 kit (lib/cmake/Qt6/Qt6Config.cmake is missing)."
}

$kit = Split-Path -Leaf ($QtDir.TrimEnd('/', '\'))
$adsRoot = Join-Path $repoRoot "build/ads-$kit"
$install = Join-Path $adsRoot 'install'

# Only ever delete inside this project's build/ folder.
if ($Clean -and (Test-Path -LiteralPath $adsRoot)) {
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'build')) + [IO.Path]::DirectorySeparatorChar
    if (-not ([IO.Path]::GetFullPath($adsRoot) + [IO.Path]::DirectorySeparatorChar).StartsWith($buildRoot)) {
        throw "build-ads: refusing to delete '$adsRoot' outside the project's build folder."
    }
    Remove-Item -LiteralPath $adsRoot -Recurse -Force
}

# Toolchain.
$cmakeDir = 'C:\Qt\Tools\CMake_64\bin'
$ninjaDir = 'C:\Qt\Tools\Ninja'
if ($kit -match 'mingw') {
    $mingwBin = Get-ChildItem -Directory 'C:\Qt\Tools' -Filter 'mingw*_64' -ErrorAction SilentlyContinue |
        Sort-Object Name | Select-Object -Last 1
    if (-not $mingwBin) { throw "build-ads: no MinGW toolchain under C:\Qt\Tools (mingw*_64)." }
    $env:PATH = "$($mingwBin.FullName)\bin;$cmakeDir;$ninjaDir;$env:PATH"
} else {
    . (Join-Path $PSScriptRoot 'vsdev.ps1')
    $env:PATH = "$cmakeDir;$ninjaDir;$env:PATH"
}

New-Item -ItemType Directory -Force -Path $adsRoot | Out-Null

foreach ($config in @('Debug', 'Release')) {
    $buildDir = Join-Path $adsRoot ("build-" + $config.ToLower())
    Write-Host "== build-ads: $kit $config =="
    & cmake -S $SourceDir -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$config" `
        "-DCMAKE_PREFIX_PATH=$QtDir" "-DCMAKE_INSTALL_PREFIX=$install" -DBUILD_EXAMPLES=OFF
    if ($LASTEXITCODE -ne 0) { throw "build-ads: configure ($config) failed with exit code $LASTEXITCODE" }
    & cmake --build $buildDir --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "build-ads: build ($config) failed with exit code $LASTEXITCODE" }
    & cmake --install $buildDir
    if ($LASTEXITCODE -ne 0) { throw "build-ads: install ($config) failed with exit code $LASTEXITCODE" }
}

$installFull = (Resolve-Path -LiteralPath $install).Path.Replace('\', '/')
Write-Host ''
Write-Host "== build-ads: done =="
Write-Host "MC_ADS_DIR = $installFull"
Write-Host "  CMake : -DMC_ADS_DIR=$installFull   (or set the environment variable MC_ADS_DIR)"
if ($kit -match 'mingw') {
    Write-Host "  qmake : MC_ADS_DIR_MINGW = $installFull   (in mc_local.pri)"
} else {
    Write-Host "  qmake : MC_ADS_DIR = $installFull   (in mc_local.pri)"
}
