[CmdletBinding()]
param(
    [string]$Compiler = "",
    [string]$Toolchain = "",
    [string]$OutDir = ".\build",
    [switch]$SkipSanitizers
)

$ErrorActionPreference = "Stop"
$hostExecutable = (Get-Process -Id $PID).Path
$buildDirectory = if ([System.IO.Path]::IsPathRooted($OutDir)) {
    [System.IO.Path]::GetFullPath($OutDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot $OutDir))
}

function Invoke-ProjectScript {
    param(
        [string]$Label,
        [string]$Script,
        [string[]]$Arguments = @()
    )

    Write-Host "`n== $Label =="
    & $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $Script @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Label a echoue avec le code $LASTEXITCODE."
    }
}

$testArguments = @("-WarningsAsErrors", "-StrictCore", "-OutDir", (Join-Path $buildDirectory "native"))
if ($Compiler) { $testArguments += @("-Compiler", $Compiler) }
Invoke-ProjectScript -Label "Tests natifs" -Script (Join-Path $PSScriptRoot "test.ps1") -Arguments $testArguments

if (!$SkipSanitizers) {
    $sanitizerArguments = @("-Sanitize", "-OutDir", (Join-Path $buildDirectory "sanitizers"))
    if ($Compiler) { $sanitizerArguments += @("-Compiler", $Compiler) }
    Invoke-ProjectScript -Label "ASan + UBSan" -Script (Join-Path $PSScriptRoot "test.ps1") -Arguments $sanitizerArguments
}

$buildArguments = @("-OutDir", $buildDirectory)
if ($Toolchain) { $buildArguments += @("-Toolchain", $Toolchain) }
Invoke-ProjectScript -Label "Build et verification ARM" -Script (Join-Path $PSScriptRoot "build.ps1") -Arguments $buildArguments
$armObject = Join-Path $buildDirectory "WideSmearNT.o"
$firstArmHash = (Get-FileHash -LiteralPath $armObject -Algorithm SHA256).Hash
Invoke-ProjectScript -Label "Remplacement de l'objet ARM existant" -Script (Join-Path $PSScriptRoot "build.ps1") -Arguments $buildArguments
if ((Get-FileHash -LiteralPath $armObject -Algorithm SHA256).Hash -ne $firstArmHash) {
    throw "Le rebuild ARM a produit un objet different sans changement de sources."
}
Invoke-ProjectScript -Label "Protection du build en cas d'echec" -Script (Join-Path $PSScriptRoot "tools\test_build.ps1") -Arguments @(
    "-OutDir", (Join-Path $buildDirectory "build-safety-test"))

$impulsePath = Join-Path $buildDirectory "check-impulse-24.wav"
$sweepPath = Join-Path $buildDirectory "check-sweep-16.wav"
$renderScript = Join-Path $PSScriptRoot "tools\render.ps1"
$compilerArguments = @("-OutDir", (Join-Path $buildDirectory "renderer"))
if ($Compiler) { $compilerArguments += @("-Compiler", $Compiler) }
Invoke-ProjectScript -Label "Rendu impulsion" -Script $renderScript -Arguments (
    @("-Source", "impulse", "-Duration", "0.1", "-BitDepth", "24", "-FailOnClip", "-Output", $impulsePath) + $compilerArguments)
Invoke-ProjectScript -Label "Rendu sweep" -Script $renderScript -Arguments (
    @("-Source", "sweep", "-Duration", "0.1", "-BitDepth", "16", "-FailOnClip", "-SweepEnd", "18000", "-Output", $sweepPath) + $compilerArguments)

$wavValidator = Join-Path $PSScriptRoot "tools\assert_wav.ps1"
& $wavValidator -Path $impulsePath -ExpectedSampleRate 48000 -ExpectedBitDepth 24 -ExpectedFrames 4800
& $wavValidator -Path $sweepPath -ExpectedSampleRate 48000 -ExpectedBitDepth 16 -ExpectedFrames 4800
$rendererName = if ([System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT) { "smear_render.exe" } else { "smear_render" }
Invoke-ProjectScript -Label "Limites du renderer" -Script (Join-Path $PSScriptRoot "tools\test_renderer.ps1") -Arguments @(
    "-Executable", (Join-Path $buildDirectory "renderer\$rendererName"),
    "-OutDir", (Join-Path $buildDirectory "renderer-regressions"))
Write-Host "`nVerification hors ligne complete."
