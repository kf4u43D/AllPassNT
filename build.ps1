[CmdletBinding()]
param(
    [string]$Toolchain = "",
    [string]$NtApiPath = "",
    [string]$OutDir = ".\build",
    [ValidatePattern('^[A-Za-z0-9_-]+\.o$')]
    [string]$Target = "smearnt_nt.o",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

function Resolve-Toolchain {
    param([string]$Requested)

    if ($Requested) {
        if (Test-Path -LiteralPath $Requested) {
            return (Resolve-Path -LiteralPath $Requested).Path
        }
        $requestedCommand = Get-Command $Requested -ErrorAction SilentlyContinue
        if ($requestedCommand) {
            return $requestedCommand.Source
        }
        throw "Compilateur ARM introuvable: $Requested"
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
$outputDirectory = if ([System.IO.Path]::IsPathRooted($OutDir)) {
    [System.IO.Path]::GetFullPath($OutDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutDir))
}
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
$srcInclude = Join-Path $repoRoot "src"
$apiHeaderForBuild = Join-Path $localInclude "distingnt\api.h"

if (!(Test-Path -LiteralPath $apiHeaderForBuild)) {
    throw "Header disting NT introuvable: include\distingnt\api.h"
}

$includeArguments = @()
if ($NtApiPath) {
    $sdkRoot = if ([System.IO.Path]::IsPathRooted($NtApiPath)) {
        [System.IO.Path]::GetFullPath($NtApiPath)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $repoRoot $NtApiPath))
    }
    $sdkInclude = Join-Path $sdkRoot "include"
    if (!(Test-Path -LiteralPath (Join-Path $sdkInclude "distingnt\api.h"))) {
        throw "Header disting NT introuvable dans le SDK: $sdkInclude"
    }
    $apiHeaderForBuild = Join-Path $sdkInclude "distingnt\api.h"
    $includeArguments += "-I$sdkInclude"
}
$includeArguments += @("-I$localInclude", "-I$srcInclude")

& (Join-Path $repoRoot "tools\assert_api.ps1") -Path $apiHeaderForBuild
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$temporaryOutputPath = Join-Path $outputDirectory ("{0}.{1}.tmp" -f $Target, [System.Guid]::NewGuid().ToString("N"))

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
    "-Wextra",
    "-r",
    "-nostdlib"
) + $includeArguments + $sources + @("-o", $temporaryOutputPath)

Write-Host "Build: $outputPath"
$compilerDirectory = Split-Path -Parent $compiler
$nmName = if ([System.IO.Path]::GetExtension($compiler) -eq ".exe") {
    "arm-none-eabi-nm.exe"
} else {
    "arm-none-eabi-nm"
}
$nmPath = Join-Path $compilerDirectory $nmName
$readElfName = if ([System.IO.Path]::GetExtension($compiler) -eq ".exe") {
    "arm-none-eabi-readelf.exe"
} else {
    "arm-none-eabi-readelf"
}
$readElfPath = Join-Path $compilerDirectory $readElfName
try {
    & $compiler @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation ARM echouee avec le code $LASTEXITCODE."
    }
    & (Join-Path $repoRoot "verify.ps1") -ObjectPath $temporaryOutputPath -Nm $nmPath -ReadElf $readElfPath -ApiHeaderPath $apiHeaderForBuild
    if (Test-Path -LiteralPath $outputPath -PathType Leaf) {
        [System.IO.File]::Replace($temporaryOutputPath, $outputPath, [System.Management.Automation.Language.NullString]::Value)
    } else {
        [System.IO.File]::Move($temporaryOutputPath, $outputPath)
    }
} finally {
    if (Test-Path -LiteralPath $temporaryOutputPath -PathType Leaf) {
        Remove-Item -LiteralPath $temporaryOutputPath -Force
    }
}
