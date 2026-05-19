#Requires -Version 5.1
<#
.SYNOPSIS
    Builds the Projucer (JUCE project tool) via CMake.

.DESCRIPTION
    Loads the Visual Studio developer environment, then configures JUCE with
    JUCE_BUILD_EXTRAS=ON in a dedicated build tree and builds the Projucer
    target. Requires the androtone project to have been configured at least
    once so that CPM has fetched JUCE into build/_deps/juce-src.

.PARAMETER Config
    CMake build configuration. Defaults to Release.

.PARAMETER Clean
    Delete the Projucer build tree before configuring.
#>

[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$juceSrc     = Join-Path $projectRoot 'cmake-build-debug-visual-studio\_deps\juce-src'
$buildDir    = Join-Path $projectRoot 'cmake-build-debug-visual-studio\projucer'

if (-not (Test-Path $juceSrc)) {
    throw "JUCE source not found at '$juceSrc'. Run 'cmake -S . -B build' from the project root first so CPM fetches JUCE."
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at '$vswhere'. Install Visual Studio (any recent edition) or the VS Build Tools."
}

$vsInstall = & $vswhere -latest -prerelease -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if (-not $vsInstall) {
    throw 'Could not find a Visual Studio installation with the C++ x64 toolchain.'
}

$devShell = Join-Path $vsInstall 'Common7\Tools\Launch-VsDevShell.ps1'
if (-not (Test-Path $devShell)) {
    throw "Launch-VsDevShell.ps1 not found at '$devShell'."
}

Write-Host "Loading VS developer environment from $vsInstall"
& $devShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Removing $buildDir"
    Remove-Item -Recurse -Force $buildDir
}

Write-Host "Configuring Projucer in $buildDir"
cmake -S $juceSrc -B $buildDir -G 'Ninja Multi-Config' -DJUCE_BUILD_EXTRAS=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

Write-Host "Building Projucer ($Config)"
cmake --build $buildDir --target Projucer --config $Config
if ($LASTEXITCODE -ne 0) { throw 'CMake build failed' }

$exe = Get-ChildItem -Path $buildDir -Recurse -Filter Projucer.exe -ErrorAction SilentlyContinue |
       Select-Object -First 1

if ($exe) {
    Write-Host "Built: $($exe.FullName)"
} else {
    Write-Warning 'Build reported success but Projucer.exe was not found under the build tree.'
}
