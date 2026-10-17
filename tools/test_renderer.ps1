[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [string]$OutDir = ".\build\renderer-regressions"
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$renderer = (Resolve-Path -LiteralPath $Executable).Path
$outputDirectory = if ([System.IO.Path]::IsPathRooted($OutDir)) {
    [System.IO.Path]::GetFullPath($OutDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutDir))
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

function Invoke-Renderer {
    param([string[]]$Arguments)
    $ErrorActionPreference = "Continue"
    $PSNativeCommandUseErrorActionPreference = $false
    $messages = @(& $renderer @Arguments 2>&1)
    return @{ ExitCode = $LASTEXITCODE; Messages = $messages }
}

$invalidCases = @(
    @{ Name = "duration-overflow"; Arguments = @("--duration", "1e99") },
    @{ Name = "uint64-boundary"; Arguments = @("--duration", "3.8430716820228233e14") },
    @{ Name = "empty-duration"; Arguments = @("--duration=") },
    @{ Name = "empty-help"; Arguments = @("--help=") }
)
foreach ($case in $invalidCases) {
    $path = Join-Path $outputDirectory ($case.Name + ".wav")
    # A rejected request must also preserve a previous render at that path.
    [System.IO.File]::WriteAllText($path, "existing-render")
    $hashBefore = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $arguments = @("--output", $path) + $case.Arguments
    $result = Invoke-Renderer -Arguments $arguments
    if ($result.ExitCode -ne 1) {
        throw "Renderer: $($case.Name) devrait echouer avec le code 1: $($result.Messages -join ' ')"
    }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $hashBefore) {
        throw "Renderer: $($case.Name) a modifie le rendu existant."
    }
    Remove-Item -LiteralPath $path
}

$sweepPath = Join-Path $outputDirectory "extreme-sweep.wav"
$arguments = @("--source", "sweep", "--output", $sweepPath,
    "--duration", "0.02", "--sample-rate", "48000", "--bit-depth", "24",
    "--sweep-start", "1e-307", "--sweep-end", "20000", "--amplitude", "0.1",
    "--stages", "0", "--feedback", "0")
$result = Invoke-Renderer -Arguments $arguments
if ($result.ExitCode -ne 0) {
    throw "Renderer: sweep extreme rejete: $($result.Messages -join ' ')"
}
& (Join-Path $PSScriptRoot "assert_wav.ps1") -Path $sweepPath -ExpectedSampleRate 48000 -ExpectedBitDepth 24 -ExpectedFrames 960
Write-Host "Renderer: 4 rejets sans ecrasement et sweep extreme valides."
