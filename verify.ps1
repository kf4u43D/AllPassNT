[CmdletBinding()]
param(
    [string]$ObjectPath = ".\build\WideSmearNT.o",
    [string]$Nm = "",
    [string]$ReadElf = "",
    [string]$ApiHeaderPath = ""
)

$ErrorActionPreference = "Stop"

function Resolve-Nm {
    param([string]$Requested)

    if ($Requested) {
        if (!(Test-Path -LiteralPath $Requested)) {
            $requestedCommand = Get-Command $Requested -ErrorAction SilentlyContinue
            if ($requestedCommand) { return $requestedCommand.Source }
            throw "arm-none-eabi-nm introuvable: $Requested"
        }
        return (Resolve-Path -LiteralPath $Requested).Path
    }

    $fromPath = Get-Command "arm-none-eabi-nm" -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    $defaultPath = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.3 rel1\bin\arm-none-eabi-nm.exe"
    if (Test-Path -LiteralPath $defaultPath) {
        return $defaultPath
    }

    throw "arm-none-eabi-nm introuvable. Passe le chemin avec -Nm."
}

function Resolve-ReadElf {
    param(
        [string]$Requested,
        [string]$ResolvedNm
    )

    if ($Requested) {
        if (!(Test-Path -LiteralPath $Requested)) {
            $requestedCommand = Get-Command $Requested -ErrorAction SilentlyContinue
            if ($requestedCommand) { return $requestedCommand.Source }
            throw "arm-none-eabi-readelf introuvable: $Requested"
        }
        return (Resolve-Path -LiteralPath $Requested).Path
    }

    $siblingName = if ([System.IO.Path]::GetExtension($ResolvedNm) -eq ".exe") {
        "arm-none-eabi-readelf.exe"
    } else {
        "arm-none-eabi-readelf"
    }
    $sibling = Join-Path (Split-Path -Parent $ResolvedNm) $siblingName
    if (Test-Path -LiteralPath $sibling) {
        return (Resolve-Path -LiteralPath $sibling).Path
    }

    $fromPath = Get-Command "arm-none-eabi-readelf" -ErrorAction SilentlyContinue
    if ($fromPath) {
        return $fromPath.Source
    }

    throw "arm-none-eabi-readelf introuvable. Passe le chemin avec -ReadElf."
}

$apiHeader = if ($ApiHeaderPath) {
    if ([System.IO.Path]::IsPathRooted($ApiHeaderPath)) {
        [System.IO.Path]::GetFullPath($ApiHeaderPath)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $ApiHeaderPath))
    }
} else {
    Join-Path $PSScriptRoot "include\distingnt\api.h"
}
& (Join-Path $PSScriptRoot "tools\assert_api.ps1") -Path $apiHeader

$resolvedObject = if ([System.IO.Path]::IsPathRooted($ObjectPath)) {
    [System.IO.Path]::GetFullPath($ObjectPath)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $ObjectPath))
}
if (!(Test-Path -LiteralPath $resolvedObject -PathType Leaf)) {
    throw "Objet ARM introuvable: $resolvedObject"
}
if ((Get-Item -LiteralPath $resolvedObject).Length -eq 0) {
    throw "Objet ARM vide: $resolvedObject"
}

$nmPath = Resolve-Nm -Requested $Nm
$readElfPath = Resolve-ReadElf -Requested $ReadElf -ResolvedNm $nmPath

$elfHeader = @(& $readElfPath -h $resolvedObject)
if ($LASTEXITCODE -ne 0) {
    throw "Impossible d'inspecter l'en-tete ELF."
}
$elfHeaderText = $elfHeader -join "`n"
$requiredHeaderPatterns = @(
    @{ Pattern = '(?m)^\s*Class:\s+ELF32\s*$'; Description = "classe ELF32" },
    @{ Pattern = '(?m)^\s*Data:\s+2''s complement, little endian\s*$'; Description = "boutisme little-endian" },
    @{ Pattern = '(?m)^\s*Type:\s+REL\b'; Description = "objet relogeable" },
    @{ Pattern = '(?m)^\s*Machine:\s+ARM\s*$'; Description = "architecture ARM" }
)
foreach ($required in $requiredHeaderPatterns) {
    if ($elfHeaderText -notmatch $required.Pattern) {
        throw "Format ARM invalide: $($required.Description) absent."
    }
}

$armAttributes = @(& $readElfPath -A $resolvedObject)
if ($LASTEXITCODE -ne 0) {
    throw "Impossible d'inspecter les attributs ARM."
}
$armAttributesText = $armAttributes -join "`n"
$requiredAttributePatterns = @(
    @{ Pattern = '(?m)^\s*Tag_CPU_arch:\s+v7E-M\s*$'; Description = "CPU ARMv7E-M" },
    @{ Pattern = '(?m)^\s*Tag_THUMB_ISA_use:\s+Thumb-2\s*$'; Description = "ISA Thumb-2" },
    @{ Pattern = '(?m)^\s*Tag_FP_arch:\s+FPv5/FP-D16'; Description = "FPU FPv5-D16" },
    @{ Pattern = '(?m)^\s*Tag_ABI_VFP_args:\s+VFP registers\s*$'; Description = "ABI flottante hard-float" }
)
foreach ($required in $requiredAttributePatterns) {
    if ($armAttributesText -notmatch $required.Pattern) {
        throw "Attribut ARM invalide: $($required.Description) absent."
    }
}

$defined = @(& $nmPath --extern-only --defined-only $resolvedObject)
if ($LASTEXITCODE -ne 0) {
    throw "Impossible d'inspecter les symboles definis."
}
if (($defined -join "`n") -notmatch '(?m)\bT\s+pluginEntry$') {
    throw "Export global pluginEntry absent de l'objet ARM."
}

$undefinedOutput = @(& $nmPath --undefined-only $resolvedObject)
if ($LASTEXITCODE -ne 0) {
    throw "Impossible d'inspecter les symboles non resolus."
}
$undefinedSymbols = @(
    foreach ($line in $undefinedOutput) {
        if ($line -match '\b[Uwv]\s+(\S+)$') {
            $Matches[1]
        }
    }
)

$allowedUndefined = @(
    "_GLOBAL_OFFSET_TABLE_",
    "NT_globals",
    "expf",
    "memset"
)
$unexpected = @($undefinedSymbols | Where-Object { $_ -notin $allowedUndefined })
if ($unexpected.Count -ne 0) {
    throw "Symboles externes inattendus: $($unexpected -join ', ')"
}
if ($undefinedSymbols -notcontains "NT_globals") {
    throw "NT_globals devrait rester fourni par l'hote, mais n'est pas non resolu."
}

Write-Host "Verify: API v13 intacte; ELF ARMv7E-M/Thumb-2/FPv5 hard-float; pluginEntry exporte; symboles externes autorises ($($undefinedSymbols -join ', '))."
