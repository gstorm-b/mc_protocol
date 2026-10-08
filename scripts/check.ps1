<#
.SYNOPSIS
    scripts/check.ps1 -QtDir <kit> — one command that proves the whole folder
    (SPEC-build-packaging.md, "Everything, one command").

.DESCRIPTION
    Runs five stages in order, each in its own fresh build/check-* folder, and stops at the
    first failure, printing "FAILED: <stage>" and exiting 1:

      1. cmake-full     — full CMake + Qt configure, build, ctest (BLD-01, BLD-09); then
                           asserts CMAKE_PROJECT_VERSION (from CMakeCache.txt) equals
                           MC_VERSION_STRING (from include/mc/version.h) (BLD-08, script half).
      2. cmake-core     — MC_BUILD_DEVICE=OFF, CMAKE_PREFIX_PATH unset and the Qt kit's own
                           directory removed from PATH; configure, build, ctest; fails if the
                           --trace-expand output contains a find_package() whose package name
                           starts with Qt5, Qt6 or QT (BLD-02).
      3. qmake          — <kit>/bin/qmake.exe mc_protocol.pro, then jom (MSVC; nmake if no jom
                           is found) or mingw32-make (MinGW), then the same tool with "check"
                           (BLD-03).
      4. consumer-cmake — configures, builds and runs tests/consumer_cmake as its own project;
                           fails if its build tree registers any library test (BLD-06).
      5. consumer-qmake — builds and runs tests/consumer_qmake/app.pro (BLD-07).

    The toolchain is picked from the -QtDir kit name: a name containing "msvc" dot-sources
    scripts/vsdev.ps1 (the VS developer environment); a name containing "mingw" prepends
    -MinGWDir, plus the Qt-bundled CMake and Ninja, to PATH.

    Every build is parallel: -Jobs (default: the logical processor count) feeds
    "cmake --build --parallel", "jom -j" and "mingw32-make -j"; ctest runs with -j 8.

.PARAMETER QtDir
    Path to the Qt kit to build against, e.g. C:/Qt/6.11.1/msvc2022_64 or
    C:/Qt/6.11.1/mingw_64. Its name must contain "msvc" or "mingw" so the script can tell
    which toolchain to load.

.PARAMETER MinGWDir
    MinGW bin directory, used only when -QtDir names a mingw kit. Defaults to this machine's
    known MinGW GCC 13.1 install (agent-team/project/build-env.md).

.PARAMETER Jobs
    Parallel job count for every build and test step. Defaults to the logical processor count
    ([Environment]::ProcessorCount). CMake stages run "cmake --build --parallel <Jobs>"; ctest
    runs with -j 8 (device tests use OS-chosen ports; serial tests share a RESOURCE_LOCK).

.PARAMETER JomPath
    jom.exe used for the qmake stages with the MSVC kit (jom -j <Jobs>). Defaults to the Qt
    Creator copy. If that file is missing, a jom on PATH is used; if there is none either, the
    script falls back to single-threaded nmake and prints a note. MinGW kits use
    "mingw32-make -j<Jobs>" and ignore this parameter.

.PARAMETER VcVarsVer
    MSVC kits only: the MSVC toolset to load (vsdev.ps1 -VcVarsVer), e.g. 14.44. Empty means the
    installation's default toolset for a Qt 6 kit and 14.44 for a Qt 5 kit (one with
    lib/cmake/Qt5/Qt5Config.cmake): Qt 5.15's headers do not compile with the 14.50+ (VS 2026)
    standard library. Run from a fresh PowerShell: vsdev.ps1 refuses a shell that already has
    another toolset loaded.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$QtDir,

    [string]$MinGWDir = 'C:\Qt\Tools\mingw1310_64\bin',

    [ValidateRange(1, 1024)]
    [int]$Jobs = [Environment]::ProcessorCount,

    [string]$JomPath = 'C:\Qt\Tools\QtCreator\bin\jom\jom.exe',

    [string]$VcVarsVer = ''
)

$ErrorActionPreference = 'Stop'

# Run from the repository root regardless of the caller's working directory, so every
# relative path below (build/check-*, mc_protocol.pro, tests/...) resolves the same way.
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$QtDir = $QtDir.TrimEnd('\', '/')

$totalStages = 5

function Write-Stage {
    param([int]$Index, [string]$Name)
    Write-Host "== [$Index/$totalStages] $Name"
}

# Prints the required "FAILED: <stage>" line (plus optional diagnostic detail) and stops the
# whole script with exit code 1; later stages never run.
function Stop-Stage {
    param([string]$Name, [string]$Detail)
    Write-Host "FAILED: $Name"
    if ($Detail) {
        Write-Host $Detail
    }
    exit 1
}

# Runs one native command and stops the given stage if it exits non-zero.
function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$StageName,
        [Parameter(Mandatory = $true)][string]$Exe,
        [string[]]$Arguments = @()
    )
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        Stop-Stage -Name $StageName -Detail "$Exe $($Arguments -join ' ') exited with code $LASTEXITCODE"
    }
}

# ---- toolchain selection from the -QtDir kit name ----
$isMsvc = $QtDir -match '(?i)msvc'
$isMingw = $QtDir -match '(?i)mingw'

if (-not $isMsvc -and -not $isMingw) {
    Write-Error "check.ps1: cannot tell the toolchain from -QtDir '$QtDir' (expected its name to contain 'msvc' or 'mingw')."
    exit 1
}

if ($isMsvc) {
    # Qt 5.15's headers do not compile with the 14.50+ (VS 2026) standard library: a Qt 5 kit gets
    # an older toolset unless -VcVarsVer names one.
    $isQt5Kit = Test-Path -LiteralPath (Join-Path $QtDir 'lib/cmake/Qt5/Qt5Config.cmake')
    if (-not $VcVarsVer -and $isQt5Kit) {
        $VcVarsVer = '14.44'
    }
    $mcVcVarsVer = $VcVarsVer
    . "$PSScriptRoot\vsdev.ps1" -VcVarsVer $mcVcVarsVer
    if ($mcVcVarsVer -and -not ("$env:VCToolsVersion".StartsWith($mcVcVarsVer))) {
        Write-Error "check.ps1: could not load the MSVC $mcVcVarsVer toolset (loaded: '$env:VCToolsVersion')."
        exit 1
    }
    if ($mcVcVarsVer) {
        Write-Host "check.ps1: MSVC toolset $env:VCToolsVersion"
    }
    # jom is an nmake-compatible parallel make: the given path first, then PATH, else nmake.
    if (Test-Path -LiteralPath $JomPath -PathType Leaf) {
        $makeTool = $JomPath
        $makeArgs = @('-j', "$Jobs")
    }
    elseif (Get-Command 'jom' -ErrorAction SilentlyContinue) {
        $makeTool = 'jom'
        $makeArgs = @('-j', "$Jobs")
    }
    else {
        Write-Host ("check.ps1: jom not found ('$JomPath' or on PATH); " +
            "qmake stages use nmake, single-threaded.")
        $makeTool = 'nmake'
        $makeArgs = @()
    }
}
else {
    $env:PATH = "$MinGWDir;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;$env:PATH"
    $makeTool = 'mingw32-make'
    $makeArgs = @("-j$Jobs")
}

# Never inherited from env for any stage below: every CMake configure passes
# -DCMAKE_PREFIX_PATH explicitly (or omits it, for the cmake-core stage) instead.
Remove-Item Env:\CMAKE_PREFIX_PATH -ErrorAction SilentlyContinue

$qmakeExe = Join-Path $QtDir 'bin/qmake.exe'

# ---- stage 1: cmake-full (BLD-01, BLD-09, BLD-08 script half) ----
Write-Stage -Index 1 -Name 'cmake-full'
$dir = 'build/check-cmake-full'
Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
Invoke-Checked -StageName 'cmake-full' -Exe 'cmake' -Arguments @(
    '-S', '.', '-B', $dir, '-G', 'Ninja',
    "-DCMAKE_PREFIX_PATH=$QtDir", '-DCMAKE_BUILD_TYPE=Debug'
)
Invoke-Checked -StageName 'cmake-full' -Exe 'cmake' `
    -Arguments @('--build', $dir, '--parallel', "$Jobs")
Invoke-Checked -StageName 'cmake-full' -Exe 'ctest' `
    -Arguments @('--test-dir', $dir, '--output-on-failure', '-j', '8')

$versionHeader = Get-Content 'include/mc/version.h' -Raw
if ($versionHeader -notmatch '#define MC_VERSION_STRING "([^"]+)"') {
    Stop-Stage -Name 'cmake-full' -Detail 'cannot find MC_VERSION_STRING in include/mc/version.h'
}
$headerVersion = $Matches[1]

$cacheContent = Get-Content (Join-Path $dir 'CMakeCache.txt') -Raw
if ($cacheContent -notmatch '(?m)^CMAKE_PROJECT_VERSION:STATIC=(.*)$') {
    Stop-Stage -Name 'cmake-full' -Detail "CMAKE_PROJECT_VERSION not found in $dir/CMakeCache.txt"
}
$cacheVersion = $Matches[1].Trim()

if ($cacheVersion -ne $headerVersion) {
    Stop-Stage -Name 'cmake-full' -Detail "CMAKE_PROJECT_VERSION ('$cacheVersion') != MC_VERSION_STRING ('$headerVersion')"
}

# ---- stage 2: cmake-core (BLD-02) ----
Write-Stage -Index 2 -Name 'cmake-core'
$dir = 'build/check-cmake-core'
Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$traceFile = Join-Path $dir 'configure-trace.log'

# Strip only PATH entries under the Qt kit itself (qmake.exe, Qt6Config.cmake's hint path):
# the CMake/Ninja/MinGW directories used to run this very script live under a different
# subtree (C:\Qt\Tools\...) and must stay on PATH so this stage can still configure and build.
$originalPath = $env:PATH
try {
    $qtDirLower = $QtDir.ToLowerInvariant()
    $filteredEntries = $env:PATH -split ';' | Where-Object {
        $_ -ne '' -and -not ($_.TrimEnd('\', '/').ToLowerInvariant().StartsWith($qtDirLower))
    }
    $env:PATH = ($filteredEntries -join ';')

    Invoke-Checked -StageName 'cmake-core' -Exe 'cmake' -Arguments @(
        '-S', '.', '-B', $dir, '-G', 'Ninja',
        '-DMC_BUILD_DEVICE=OFF', '-DCMAKE_BUILD_TYPE=Debug',
        '--trace-expand', "--trace-redirect=$traceFile"
    )

    # Anchored on the trace's own "<file>(<line>):  <command>(" prefix so this matches only an
    # actual Qt search -- any package name starting with Qt5, Qt6 or QT: find_package(Qt6 ...),
    # find_package(Qt5Core ...), the find_package(QT NAMES ...) of cmake/mc_qt.cmake -- not the
    # substring appearing inside another command's argument text (e.g. the MC_BUILD_DEVICE option()'s
    # own help string in CMakeLists.txt). Case-sensitive: a prefix match, as the BLD-02 spec row says.
    if (Select-String -LiteralPath $traceFile -Pattern '\):\s+find_package\((Qt5|Qt6|QT)' -CaseSensitive -Quiet) {
        Stop-Stage -Name 'cmake-core' -Detail "$traceFile contains a find_package call for Qt"
    }

    Invoke-Checked -StageName 'cmake-core' -Exe 'cmake' `
        -Arguments @('--build', $dir, '--parallel', "$Jobs")
    Invoke-Checked -StageName 'cmake-core' -Exe 'ctest' `
        -Arguments @('--test-dir', $dir, '--output-on-failure', '-j', '8')
}
finally {
    $env:PATH = $originalPath
}

# ---- stage 3: qmake (BLD-03) ----
Write-Stage -Index 3 -Name 'qmake'
$dir = 'build/check-qmake'
Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Push-Location $dir
try {
    Invoke-Checked -StageName 'qmake' -Exe $qmakeExe -Arguments @('../../mc_protocol.pro', 'CONFIG+=debug')
    Invoke-Checked -StageName 'qmake' -Exe $makeTool -Arguments $makeArgs
    Invoke-Checked -StageName 'qmake' -Exe $makeTool -Arguments ($makeArgs + @('check'))
}
finally {
    Pop-Location
}

# ---- stage 4: consumer-cmake (BLD-06) ----
Write-Stage -Index 4 -Name 'consumer-cmake'
$dir = 'build/check-consumer-cmake'
Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
Invoke-Checked -StageName 'consumer-cmake' -Exe 'cmake' -Arguments @(
    '-S', 'tests/consumer_cmake', '-B', $dir, '-G', 'Ninja',
    "-DCMAKE_PREFIX_PATH=$QtDir", '-DCMAKE_BUILD_TYPE=Debug'
)
Invoke-Checked -StageName 'consumer-cmake' -Exe 'cmake' `
    -Arguments @('--build', $dir, '--parallel', "$Jobs")

# No "2>&1" here: with $ErrorActionPreference = 'Stop', merging a native command's stderr
# into the success stream can promote a routine stderr line into a terminating exception in
# PowerShell 5.1, which would abort this stage without ever printing "FAILED: consumer-cmake".
# ctest -N's own diagnostics go to stdout anyway, so nothing is lost by not merging.
$ctestDryRun = & ctest --test-dir $dir -N
$ctestDryRunExit = $LASTEXITCODE
if ($ctestDryRunExit -ne 0) {
    Stop-Stage -Name 'consumer-cmake' -Detail "ctest -N exited with code $ctestDryRunExit"
}
if (-not ($ctestDryRun -match 'Total Tests: 0')) {
    Stop-Stage -Name 'consumer-cmake' -Detail "its build tree registered a library test target: $ctestDryRun"
}

$consumerExe = Get-ChildItem -Path $dir -Filter 'consumer.exe' -Recurse | Select-Object -First 1
if (-not $consumerExe) {
    Stop-Stage -Name 'consumer-cmake' -Detail "consumer.exe not found under $dir"
}
# The consumer links mc::device, so it needs the kit's Qt DLLs at run time; a shell may have none
# on PATH (T-040), so the kit's bin directory goes in front for this one run.
$pathBeforeConsumer = $env:PATH
$env:PATH = (Join-Path $QtDir 'bin') + ';' + $env:PATH
try {
    Invoke-Checked -StageName 'consumer-cmake' -Exe $consumerExe.FullName -Arguments @()
}
finally {
    $env:PATH = $pathBeforeConsumer
}

# ---- stage 5: consumer-qmake (BLD-07) ----
Write-Stage -Index 5 -Name 'consumer-qmake'
$dir = 'build/check-consumer-qmake'
Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $dir | Out-Null
Push-Location $dir
try {
    Invoke-Checked -StageName 'consumer-qmake' -Exe $qmakeExe -Arguments @('../../tests/consumer_qmake/app.pro', 'CONFIG+=debug')
    Invoke-Checked -StageName 'consumer-qmake' -Exe $makeTool -Arguments $makeArgs
}
finally {
    Pop-Location
}

$appExe = Get-ChildItem -Path $dir -Filter 'app.exe' -Recurse | Select-Object -First 1
if (-not $appExe) {
    Stop-Stage -Name 'consumer-qmake' -Detail "app.exe not found under $dir"
}
$pathBeforeConsumer = $env:PATH
$env:PATH = (Join-Path $QtDir 'bin') + ';' + $env:PATH
try {
    Invoke-Checked -StageName 'consumer-qmake' -Exe $appExe.FullName -Arguments @()
}
finally {
    $env:PATH = $pathBeforeConsumer
}

Write-Host "== check: all $totalStages stages passed =="
exit 0
