[CmdletBinding()]
param(
    [ValidateSet("impulse", "sweep")]
    [string]$Source = "impulse",
    [string]$Output = "",
    [ValidateRange(8000, 384000)]
    [int]$SampleRate = 48000,
    [ValidateRange(0.000001, [double]::MaxValue)]
    [double]$Duration = 2.0,
    [ValidateSet(16, 24)]
    [int]$BitDepth = 24,
    [ValidateRange(0.0, 100.0)]
    [double]$Frequency = 50.0,
    [ValidateRange(0.0, 100.0)]
    [double]$Resonance = 50.0,
    [ValidateRange(0.0, 100.0)]
    [double]$Stages = 50.0,
    [ValidateRange(0.0, 100.0)]
    [double]$Distribution = 50.0,
    [ValidateRange(0.0, 100.0)]
    [double]$Feedback = 20.0,
    [ValidateRange(0.000001, [double]::MaxValue)]
    [double]$SweepStart = 20.0,
    [ValidateRange(0.000001, [double]::MaxValue)]
    [double]$SweepEnd = 20000.0,
    [ValidateRange(0.0, 1.0)]
    [double]$Amplitude = 0.8,
    [switch]$FailOnClip,
    [string]$Compiler = "",
    [string]$OutDir = ".\build"
)

$ErrorActionPreference = "Stop"
$invariantCulture = [System.Globalization.CultureInfo]::InvariantCulture

function Resolve-HostCompiler {
    param([string]$Requested)

    if ($Requested) {
        if (Test-Path -LiteralPath $Requested) {
            return (Resolve-Path -LiteralPath $Requested).Path
        }
        $requestedCommand = Get-Command $Requested -ErrorAction SilentlyContinue
        if ($requestedCommand) {
            return $requestedCommand.Source
        }
        throw "Compilateur hote introuvable: $Requested"
    }

    foreach ($candidate in @("clang++", "g++")) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) {
            return $command.Source
        }
    }

    $defaultClangPath = "C:\Program Files\LLVM\bin\clang++.exe"
    if (Test-Path -LiteralPath $defaultClangPath) {
        return $defaultClangPath
    }

    throw "Compilateur C++ hote introuvable. Installe clang++/g++ ou utilise -Compiler."
}

function Format-Invariant {
    param([double]$Value)
    return $Value.ToString("R", $invariantCulture)
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$buildDirectory = if ([System.IO.Path]::IsPathRooted($OutDir)) {
    [System.IO.Path]::GetFullPath($OutDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutDir))
}
$isWindowsHost = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
$executableName = if ($isWindowsHost) { "smear_render.exe" } else { "smear_render" }
$executablePath = Join-Path $buildDirectory $executableName
$compilerPath = Resolve-HostCompiler -Requested $Compiler

if (-not $Output) {
    $Output = Join-Path $buildDirectory ("smear-{0}.wav" -f $Source)
} elseif (-not [System.IO.Path]::IsPathRooted($Output)) {
    $Output = Join-Path $repoRoot $Output
}
$outputPath = [System.IO.Path]::GetFullPath($Output)
$outputDirectory = Split-Path -Parent $outputPath

New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
if ($outputDirectory) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$sources = @(
    "tools\smear_render.cpp",
    "src\widesmearnt_core\SmearCore.cpp",
    "src\widesmearnt_core\AllpassNetwork.cpp",
    "src\widesmearnt_core\AllpassSection.cpp",
    "src\widesmearnt_core\ParamSmoother.cpp"
) | ForEach-Object { Join-Path $repoRoot $_ }

$compilerArguments = @(
    "-std=c++14",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Werror",
    "-I$(Join-Path $repoRoot 'src')"
) + $sources + @("-o", $executablePath)

Write-Host "Compilation: $executablePath"
& $compilerPath @compilerArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$renderArguments = @(
    "--source", $Source,
    "--output", $outputPath,
    "--sample-rate", $SampleRate.ToString($invariantCulture),
    "--duration", (Format-Invariant $Duration),
    "--bit-depth", $BitDepth.ToString($invariantCulture),
    "--frequency", (Format-Invariant $Frequency),
    "--resonance", (Format-Invariant $Resonance),
    "--stages", (Format-Invariant $Stages),
    "--distribution", (Format-Invariant $Distribution),
    "--feedback", (Format-Invariant $Feedback),
    "--sweep-start", (Format-Invariant $SweepStart),
    "--sweep-end", (Format-Invariant $SweepEnd),
    "--amplitude", (Format-Invariant $Amplitude)
)
if ($FailOnClip) {
    $renderArguments += "--fail-on-clip"
}

Write-Host "Rendu: $outputPath"
& $executablePath @renderArguments
exit $LASTEXITCODE
