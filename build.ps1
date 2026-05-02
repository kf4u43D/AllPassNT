[CmdletBinding()]
param(
    [string]$Toolchain = "",
    [string]$NtApiPath = ".\distingNT_API",
    [string]$OutDir = ".\build",
    [string]$Target = "smearnt_nt.o",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Resolve-Toolchain {
    param([string]$Requested)

    if ($Requested) {
        return $Requested
    }

    $fromPath = Get-Command "arm-none-eabi-g++" -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $defaultArmPath = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.3 rel1\bin\arm-none-eabi-g++.exe"
    if (Test-Path -LiteralPath $defaultArmPath) {
        return $defaultArmPath
    }

    throw "arm-none-eabi-g++ introuvable. Passe le chemin avec -Toolchain."
}

$repoRoot = $PSScriptRoot
$outputDirectory = Join-Path $repoRoot $OutDir
$outputPath = Join-Path $outputDirectory $Target

if ($Clean) {
    if (Test-Path -LiteralPath $outputPath) {
        Remove-Item -LiteralPath $outputPath -Force
    }
    Write-Host "Clean: $outputPath"
    return
}

$compiler = Resolve-Toolchain -Requested $Toolchain
$localInclude = Join-Path $repoRoot "include"
$apiInclude = Join-Path $repoRoot (Join-Path $NtApiPath "include")
$srcInclude = Join-Path $repoRoot "src"

if (!(Test-Path -LiteralPath (Join-Path $localInclude "distingnt\api.h")) -and
    !(Test-Path -LiteralPath (Join-Path $apiInclude "distingnt\api.h"))) {
    throw "Header disting NT introuvable. Attendu dans include\distingnt\api.h ou $apiInclude."
}

New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$sources = @(
    "src\SmearNTAdapter.cpp",
    "src\smearnt_core\SmearCore.cpp",
    "src\smearnt_core\AllpassNetwork.cpp",
    "src\smearnt_core\AllpassSection.cpp",
    "src\smearnt_core\ParamSmoother.cpp"
) | ForEach-Object { Join-Path $repoRoot $_ }

$arguments = @(
    "-std=c++11",
    "-mcpu=cortex-m7",
    "-mfpu=fpv5-d16",
    "-mfloat-abi=hard",
    "-mthumb",
    "-fno-rtti",
    "-fno-exceptions",
    "-Os",
    "-fPIC",
    "-Wall",
    "-I$localInclude",
    "-I$apiInclude",
    "-I$srcInclude",
    "-r",
    "-nostdlib"
) + $sources + @("-o", $outputPath)

Write-Host "Build: $outputPath"
& $compiler @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
