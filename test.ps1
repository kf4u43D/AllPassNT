[CmdletBinding()]
param(
    [string]$Compiler = "",
    [string]$OutDir = ".\build",
    [switch]$Sanitize,
    [switch]$WarningsAsErrors,
    [switch]$StrictCore
)

$ErrorActionPreference = "Stop"

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

$repoRoot = $PSScriptRoot
$outputDirectory = if ([System.IO.Path]::IsPathRooted($OutDir)) {
    [System.IO.Path]::GetFullPath($OutDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutDir))
}
$outputName = if ($Sanitize) { "smearnt_tests_san.exe" } else { "smearnt_tests.exe" }
$outputPath = Join-Path $outputDirectory $outputName
$compilerPath = Resolve-HostCompiler -Requested $Compiler
$compilerVersion = (& $compilerPath --version 2>&1) -join "`n"
$isClang = $compilerVersion -match '(?i)clang'
$isWindowsHost = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT

New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$sources = @(
    "tests\smearnt_tests.cpp",
    "src\NTHostShim.cpp",
    "src\SmearNTAdapter.cpp",
    "src\smearnt_core\SmearCore.cpp",
    "src\smearnt_core\AllpassNetwork.cpp",
    "src\smearnt_core\AllpassSection.cpp",
    "src\smearnt_core\ParamSmoother.cpp"
) | ForEach-Object { Join-Path $repoRoot $_ }

$arguments = @(
    "-std=c++14",
    "-Wall",
    "-Wextra",
    "-I$(Join-Path $repoRoot 'include')",
    "-I$(Join-Path $repoRoot 'src')"
)
if ($isClang) {
    $arguments += @("-Wpedantic", "-Wno-c++20-designator")
}
if ($WarningsAsErrors -or $Sanitize) {
    $arguments += "-Werror"
}
if ($Sanitize) {
    $arguments += @(
        "-O1",
        "-g",
        "-fsanitize=address,undefined",
        "-fno-sanitize-recover=all",
        "-fno-omit-frame-pointer",
        "-fno-optimize-sibling-calls"
    )
    if ($isWindowsHost -and $isClang) {
        # Some Clang drivers still add libcmt when the DLL CRT is requested.
        # Exclude it so the sanitizer build consistently uses the DLL runtime.
        $arguments += @("-fms-runtime-lib=dll", "-Wl,/NODEFAULTLIB:libcmt")
    }
} else {
    $arguments += "-O2"
}
$arguments += $sources + @("-o", $outputPath)

if ($StrictCore) {
    $strictSources = @(
        "src\smearnt_core\SmearCore.cpp",
        "src\smearnt_core\AllpassNetwork.cpp",
        "src\smearnt_core\AllpassSection.cpp",
        "src\smearnt_core\ParamSmoother.cpp"
    ) | ForEach-Object { Join-Path $repoRoot $_ }
    $strictArguments = @(
        "-std=c++14",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
        "-Wconversion",
        "-Wshadow",
        "-Werror",
        "-I$(Join-Path $repoRoot 'src')",
        "-fsyntax-only"
    ) + $strictSources
    Write-Host "Strict core compile"
    & $compilerPath @strictArguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "Tests: $outputPath"
& $compilerPath @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$runtimeDirectory = $null
if ($Sanitize -and $isWindowsHost -and $isClang) {
    $resourceDirectory = (& $compilerPath -print-resource-dir).Trim()
    if ($LASTEXITCODE -ne 0 -or !$resourceDirectory) {
        throw "Impossible de localiser les runtimes Clang."
    }
    $runtimeDirectory = Join-Path $resourceDirectory "lib\windows"
    if (!(Test-Path -LiteralPath (Join-Path $runtimeDirectory "clang_rt.asan_dynamic-x86_64.dll"))) {
        throw "Runtime ASan Clang introuvable sous $runtimeDirectory."
    }
}

if ($runtimeDirectory) {
    $savedAsanOptions = $env:ASAN_OPTIONS
    $savedUbsanOptions = $env:UBSAN_OPTIONS
    $savedAsanSymbolizer = $env:ASAN_SYMBOLIZER_PATH
    $savedUbsanSymbolizer = $env:UBSAN_SYMBOLIZER_PATH
    $symbolizer = Get-Command "llvm-symbolizer" -ErrorAction SilentlyContinue
    $env:ASAN_OPTIONS = "halt_on_error=1:abort_on_error=1:detect_leaks=0:detect_stack_use_after_return=1:strict_string_checks=1:symbolize=1"
    $env:UBSAN_OPTIONS = "halt_on_error=1:print_stacktrace=1:symbolize=1"
    if ($symbolizer) {
        $env:ASAN_SYMBOLIZER_PATH = $symbolizer.Source
        $env:UBSAN_SYMBOLIZER_PATH = $symbolizer.Source
    }
    Push-Location $runtimeDirectory
    try {
        & $outputPath
        $testExitCode = $LASTEXITCODE
    } finally {
        Pop-Location
        $env:ASAN_OPTIONS = $savedAsanOptions
        $env:UBSAN_OPTIONS = $savedUbsanOptions
        $env:ASAN_SYMBOLIZER_PATH = $savedAsanSymbolizer
        $env:UBSAN_SYMBOLIZER_PATH = $savedUbsanSymbolizer
    }
} else {
    & $outputPath
    $testExitCode = $LASTEXITCODE
}
exit $testExitCode
