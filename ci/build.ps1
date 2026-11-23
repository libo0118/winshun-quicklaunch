# Build QuickLaunch.exe and quicklaunch_tests.exe with MSVC x64 + Ninja (Release).
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,

    [string]$VsRoot = "",

    [string]$Output = "build"
)

$ErrorActionPreference = "Stop"

function Resolve-VsInstallation {
    param([string]$ExplicitPath)
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (-not (Test-Path -LiteralPath $ExplicitPath)) {
            throw "Specified VsRoot does not exist: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe")
    )
    $vswhere = $vswhereCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
    if (-not $vswhere) {
        $cmd = Get-Command vswhere.exe -ErrorAction SilentlyContinue
        if ($cmd) { $vswhere = $cmd.Source }
    }
    if (-not $vswhere) {
        throw "vswhere.exe not found. Pass -VsRoot explicitly or install Visual Studio Build Tools."
    }

    $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $found) {
        $found = & $vswhere -latest -products * -property installationPath
    }
    if (-not $found) {
        throw "No Visual Studio installation with MSVC x64 tools found via vswhere.exe."
    }
    return ($found | Select-Object -First 1).Trim()
}

$QtRootResolved = (Resolve-Path -LiteralPath $QtRoot).Path
$QtConfig = Join-Path $QtRootResolved "lib\cmake\Qt6\Qt6Config.cmake"
if (-not (Test-Path -LiteralPath $QtConfig)) {
    throw "Invalid -QtRoot '$QtRootResolved': missing $QtConfig"
}

$VsPath = Resolve-VsInstallation -ExplicitPath $VsRoot
$DevShellDll = Join-Path $VsPath "Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
if (-not (Test-Path -LiteralPath $DevShellDll)) {
    throw "Microsoft.VisualStudio.DevShell.dll not found at: $DevShellDll"
}

Import-Module $DevShellDll
Enter-VsDevShell -VsInstallPath $VsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null

$NinjaDir = Join-Path $VsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
if (-not (Get-Command ninja -ErrorAction SilentlyContinue) -and (Test-Path -LiteralPath $NinjaDir)) {
    $env:PATH = "$NinjaDir;$env:PATH"
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "cmake.exe not found in PATH after Enter-VsDevShell."
}
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    throw "ninja.exe not found in PATH after Enter-VsDevShell."
}

$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$BuildDir = if ([System.IO.Path]::IsPathRooted($Output)) { $Output } else { Join-Path $RepoRoot $Output }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

Write-Host "[build] RepoRoot=$RepoRoot"
Write-Host "[build] QtRoot=$QtRootResolved"
Write-Host "[build] BuildDir=$BuildDir"

& cmake -S $RepoRoot -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtRootResolved" -DBUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

& cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "CMake build failed with exit code $LASTEXITCODE" }

foreach ($binName in @("QuickLaunch.exe", "quicklaunch_tests.exe")) {
    $binPath = Join-Path $BuildDir $binName
    if (-not (Test-Path -LiteralPath $binPath) -or ((Get-Item -LiteralPath $binPath).Length -le 0)) {
        throw "Expected build artifact missing or empty: $binPath"
    }
    Write-Host "[build] Verified: $binPath"
}
