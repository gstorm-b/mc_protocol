# scripts/vsdev.ps1
#
# Dot-source this file (". scripts/vsdev.ps1") from a plain PowerShell window to make
# cl.exe, cmake.exe and ninja.exe available, without opening a dedicated "Developer
# PowerShell for VS" window.
#
# - If cl.exe is already on PATH (e.g. this is already a VS developer shell), does nothing.
# - Otherwise finds the latest Visual Studio installation that has the C++ build tools
#   (Microsoft.VisualStudio.Component.VC.Tools.x86.x64) via vswhere.exe, and loads its
#   developer environment with Launch-VsDevShell.ps1.
# - Always makes sure the Qt-bundled CMake and Ninja are on PATH.
#
# Idempotent: running it again in a shell where it already succeeded changes nothing.
# PowerShell 5.1 compatible.

function Add-MCPathIfMissing {
    param([string]$Directory)

    if (-not (Test-Path -LiteralPath $Directory)) {
        return
    }

    $entries = $env:PATH -split ';'
    if ($entries -notcontains $Directory) {
        $env:PATH = "$Directory;$env:PATH"
    }
}

if (Get-Command 'cl.exe' -ErrorAction SilentlyContinue) {
    # Already inside a VS developer shell (or cl is otherwise on PATH already): nothing to do.
}
else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

    if (-not (Test-Path -LiteralPath $vswhere)) {
        Write-Error "vsdev: vswhere.exe not found at '$vswhere'. Install Visual Studio with the 'Desktop development with C++' workload."
        return
    }

    $vsInstallPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath

    if (-not $vsInstallPath) {
        Write-Error "vsdev: no Visual Studio installation with the C++ build tools component (Microsoft.VisualStudio.Component.VC.Tools.x86.x64) was found. Install that component from the Visual Studio Installer."
        return
    }

    $launchScript = Join-Path $vsInstallPath 'Common7\Tools\Launch-VsDevShell.ps1'

    if (-not (Test-Path -LiteralPath $launchScript)) {
        Write-Error "vsdev: '$launchScript' not found under the detected Visual Studio installation ('$vsInstallPath')."
        return
    }

    & $launchScript -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

Add-MCPathIfMissing 'C:\Qt\Tools\CMake_64\bin'
Add-MCPathIfMissing 'C:\Qt\Tools\Ninja'

Remove-Item -Path Function:\Add-MCPathIfMissing -ErrorAction SilentlyContinue
