param(
    [string]$Generator = "",
    [string]$Toolset = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-CMakeVisualStudioGenerators {
    param([string]$HelpText)

    $Pattern = '(?m)^\s*\*?\s*(?<name>Visual Studio (?<major>\d+)[^=\r\n]*?)\s*='
    foreach ($GeneratorMatch in [regex]::Matches($HelpText, $Pattern)) {
        [PSCustomObject]@{
            Name = $GeneratorMatch.Groups["name"].Value.Trim()
            Major = [int]$GeneratorMatch.Groups["major"].Value
        }
    }
}

function Find-VsWhere {
    $Command = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($null -ne $Command) {
        return $Command.Source
    }

    $ProgramFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    if (-not [string]::IsNullOrWhiteSpace($ProgramFilesX86)) {
        $Candidate = Join-Path $ProgramFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $Candidate) {
            return $Candidate
        }
    }

    return $null
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake was not found in PATH. Install CMake first."
}

$CMakeHelp = (& cmake --help | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "Could not query the generators supported by CMake."
}

$CMakeGenerators = @(Get-CMakeVisualStudioGenerators $CMakeHelp)
if ($CMakeGenerators.Count -eq 0) {
    throw "This CMake installation does not provide a Visual Studio generator."
}

$SelectedGenerator = $null
if ([string]::IsNullOrWhiteSpace($Generator)) {
    $VsWhere = Find-VsWhere
    if ([string]::IsNullOrWhiteSpace($VsWhere)) {
        throw "vswhere.exe was not found. Install Visual Studio or Visual Studio Build Tools with the Desktop development with C++ workload."
    }

    $InstalledVersionText = @(& $VsWhere -all -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not query installed Visual Studio C++ toolsets with vswhere.exe."
    }

    $InstalledMajors = @(
        $InstalledVersionText |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            ForEach-Object { ([version]$_.ToString().Trim()).Major } |
            Sort-Object -Descending -Unique
    )

    foreach ($InstalledMajor in $InstalledMajors) {
        $Candidate = $CMakeGenerators |
            Where-Object { $_.Major -eq $InstalledMajor } |
            Select-Object -First 1
        if ($null -ne $Candidate) {
            $SelectedGenerator = $Candidate
            break
        }
    }

    if ($null -eq $SelectedGenerator) {
        $InstalledDescription = if ($InstalledMajors.Count -gt 0) { $InstalledMajors -join ", " } else { "none" }
        $SupportedDescription = ($CMakeGenerators | ForEach-Object { $_.Name }) -join ", "
        throw "No installed Visual Studio C++ version matches a generator supported by this CMake. Installed VS majors: $InstalledDescription. CMake generators: $SupportedDescription. Update CMake or install a matching Visual Studio Build Tools version."
    }

    $Generator = $SelectedGenerator.Name
}
else {
    $SelectedGenerator = $CMakeGenerators |
        Where-Object { $_.Name -eq $Generator } |
        Select-Object -First 1
    if ($null -eq $SelectedGenerator) {
        $SupportedDescription = ($CMakeGenerators | ForEach-Object { $_.Name }) -join ", "
        throw "CMake does not support the requested generator '$Generator'. Available Visual Studio generators: $SupportedDescription"
    }
}

Write-Host "Using CMake generator: $Generator"
if ([string]::IsNullOrWhiteSpace($Toolset)) {
    Write-Host "Using that Visual Studio version's default MSVC toolset."
}
else {
    Write-Host "Using requested MSVC toolset: $Toolset"
}

& (Join-Path $PSScriptRoot "setup.ps1")

$BuildTag = "vs$($SelectedGenerator.Major)-x64"
if (-not [string]::IsNullOrWhiteSpace($Toolset)) {
    $SafeToolset = $Toolset -replace '[^A-Za-z0-9_.-]', '_'
    $BuildTag = "$BuildTag-$SafeToolset"
}
$BuildDirectory = Join-Path $PSScriptRoot "build-$BuildTag"

$ConfigureArguments = @("-S", $PSScriptRoot, "-B", $BuildDirectory, "-G", $Generator, "-A", "x64")
if (-not [string]::IsNullOrWhiteSpace($Toolset)) {
    $ConfigureArguments += @("-T", $Toolset)
}

cmake @ConfigureArguments
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

cmake --build $BuildDirectory --config Game__Shipping__Win64 --target PalworldSrvConnect
if ($LASTEXITCODE -ne 0) { throw "PalworldSrvConnect build failed." }

$Package = Join-Path $PSScriptRoot "dist\PalworldSrvConnect"
Write-Host ""
Write-Host "Build complete: $Package"
Write-Host "Copy that folder into Pal\Binaries\Win64\ue4ss\Mods\"
