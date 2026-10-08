<#
.SYNOPSIS
    scripts/wt-sync.ps1 -Name wt1,wt2 | -All — mirror this working tree into the parallel-build
    worktrees under .wt/ (owner, 2026-10-08).

.DESCRIPTION
    The worktrees .wt/wt1 … .wt/wtN are git worktrees of this repository used only to build and test in
    parallel (several kits, check.ps1 runs, tester and reviewer at once) without sharing build folders and
    without being disturbed by edits in progress. They are mirrors: nobody edits sources in them.

    For each selected worktree the script:
      1. moves its HEAD and index to this tree's HEAD (git reset --mixed; files are not touched);
      2. copies every tracked and untracked-but-not-ignored file of this tree whose content differs
         (uncommitted edits and new files included), and deletes files this tree no longer has;
         unchanged files keep their timestamps, so builds inside the worktree stay incremental;
      3. copies the local git-ignored build settings CMakeUserPresets.json and mc_local.pri, with
         "${sourceDir}/build/ads-" and "$$PWD/build/ads-" rewritten to this tree's absolute build/ads-
         folders (ADS is built once, in this tree).

    Never copied: build/, .wt/, .claude/, reference_source/, temp-docs/ and the real HIL profiles
    (tests/hil/profiles/*.json): they are git-ignored.

    Creating a slot (once):  git worktree add --detach .wt/wt5 HEAD
    Removing a slot:         git worktree remove --force .wt/wt5

.PARAMETER Name
    Slot folder names under .wt/ (plain names such as wt1, wt3; a comma list works through -File).
    One of -Name or -All is required, so a sync never resets a slot another agent is using by accident.

.PARAMETER All
    Every slot folder under .wt/.

.EXAMPLE
    scripts/wt-sync.ps1 -Name wt2      # one slot
    scripts/wt-sync.ps1 -All           # every slot
#>
param(
    [string[]]$Name = @(),
    [switch]$All
)

$ErrorActionPreference = 'Stop'

$main = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$wtRoot = Join-Path $main '.wt'
if ((Split-Path -Leaf (Split-Path -Parent $main)) -eq '.wt') {
    throw 'wt-sync: run it from the main tree, not from inside a worktree.'
}
if (-not (Test-Path -LiteralPath $wtRoot)) {
    throw "wt-sync: no .wt folder at '$wtRoot'."
}
# "powershell -File wt-sync.ps1 -Name wt1,wt2" passes one string; accept commas either way.
$Name = @($Name | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($All) {
    if ($Name.Count -gt 0) { throw 'wt-sync: give -Name or -All, not both.' }
    $Name = @(Get-ChildItem -LiteralPath $wtRoot -Directory | ForEach-Object { $_.Name })
} elseif ($Name.Count -eq 0) {
    throw 'wt-sync: name the slots to sync (-Name wt1,wt2) or pass -All.'
}
foreach ($n in $Name) {
    # A plain folder name only: no path separators or "..", so every write stays inside .wt/.
    if ($n -notmatch '^[A-Za-z0-9_-]+$') {
        throw "wt-sync: '$n' is not a plain slot name (letters, digits, '_' and '-' only)."
    }
}

$mainGitDir = (git -C $main rev-parse --path-format=absolute --git-common-dir).Trim()
$head = (git -C $main rev-parse HEAD).Trim()

function Get-TreeFiles([string]$dir) {
    # Tracked plus untracked-but-not-ignored, as git sees them; files deleted in the tree are dropped.
    $list = git -C $dir -c core.quotepath=off ls-files -co --exclude-standard
    if ($LASTEXITCODE -ne 0) { throw "wt-sync: git ls-files failed in '$dir'." }
    $set = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($f in $list) {
        if ($f -and (Test-Path -LiteralPath (Join-Path $dir $f) -PathType Leaf)) { [void]$set.Add($f) }
    }
    return , $set
}

function Test-SameContent([string]$a, [string]$b) {
    $ia = Get-Item -LiteralPath $a
    $ib = Get-Item -LiteralPath $b
    if ($ia.Length -ne $ib.Length) { return $false }
    return (Get-FileHash -LiteralPath $a -Algorithm SHA1).Hash -eq (Get-FileHash -LiteralPath $b -Algorithm SHA1).Hash
}

$mainFiles = Get-TreeFiles $main
$mainFwd = $main.Replace('\', '/')
$utf8 = New-Object System.Text.UTF8Encoding($false)

foreach ($n in $Name) {
    $dst = Join-Path $wtRoot $n
    if (-not (Test-Path -LiteralPath (Join-Path $dst '.git') -PathType Leaf)) {
        throw "wt-sync: '$dst' is not a git worktree (create it with: git worktree add --detach .wt/$n HEAD)."
    }
    $dstGitDir = (git -C $dst rev-parse --path-format=absolute --git-common-dir).Trim()
    if ($dstGitDir -ne $mainGitDir) {
        throw "wt-sync: '$dst' belongs to another repository ($dstGitDir)."
    }

    # Slots are detached; a slot on a branch would have that branch moved by the reset below.
    git -C $dst symbolic-ref -q HEAD | Out-Null
    if ($LASTEXITCODE -eq 0) {
        throw "wt-sync: '$dst' is on a branch; slots must be detached (git -C $dst checkout --detach)."
    }

    git -C $dst reset -q --mixed $head
    if ($LASTEXITCODE -ne 0) { throw "wt-sync: git reset failed in '$dst'." }

    $copied = 0
    foreach ($f in $mainFiles) {
        $src = Join-Path $main $f
        $to = Join-Path $dst $f
        if ((Test-Path -LiteralPath $to -PathType Leaf) -and (Test-SameContent $src $to)) { continue }
        $parent = Split-Path -Parent $to
        if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
        Copy-Item -LiteralPath $src -Destination $to -Force
        $copied++
    }

    $removed = 0
    foreach ($f in (Get-TreeFiles $dst)) {
        if (-not $mainFiles.Contains($f)) {
            $gone = Join-Path $dst $f
            Remove-Item -LiteralPath $gone -Force
            $removed++
            # Drop folders the deletion left empty, up to (not including) the slot itself.
            $dir = Split-Path -Parent $gone
            while ($dir.Length -gt $dst.Length -and (Test-Path -LiteralPath $dir) -and
                   -not (Get-ChildItem -LiteralPath $dir -Force | Select-Object -First 1)) {
                Remove-Item -LiteralPath $dir -Force
                $dir = Split-Path -Parent $dir
            }
        }
    }

    foreach ($local in @('CMakeUserPresets.json', 'mc_local.pri')) {
        $src = Join-Path $main $local
        if (-not (Test-Path -LiteralPath $src)) { continue }
        $text = [System.IO.File]::ReadAllText($src)
        $text = $text.Replace('${sourceDir}/build/ads-', "$mainFwd/build/ads-").Replace('$$PWD/build/ads-', "$mainFwd/build/ads-")
        $to = Join-Path $dst $local
        if ((Test-Path -LiteralPath $to) -and ([System.IO.File]::ReadAllText($to) -ceq $text)) { continue }
        [System.IO.File]::WriteAllText($to, $text, $utf8)
        $copied++
    }

    Write-Host ("wt-sync: {0} at {1}: {2} file(s) copied, {3} removed" -f $n, $head.Substring(0, 7), $copied, $removed)
}
