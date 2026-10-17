[CmdletBinding()]
param(
    [string]$OutDir = ".\build\build-safety-test"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $repoRoot "build.ps1"
$hostExecutable = (Get-Process -Id $PID).Path
$privateRoot = if ([System.IO.Path]::IsPathRooted($OutDir)) {
    [System.IO.Path]::GetFullPath($OutDir)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repoRoot $OutDir))
}
$runDirectory = Join-Path $privateRoot ([System.Guid]::NewGuid().ToString("N"))

function Assert-Condition {
    param([bool]$Condition, [string]$Message)
    if (!$Condition) { throw $Message }
}

function Assert-WithinDirectory {
    param([string]$Candidate, [string]$Directory)
    $candidatePath = [System.IO.Path]::GetFullPath($Candidate)
    $directoryPrefix = [System.IO.Path]::GetFullPath($Directory).TrimEnd(
        [System.IO.Path]::DirectorySeparatorChar,
        [System.IO.Path]::AltDirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $comparison = if ([System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT) {
        [System.StringComparison]::OrdinalIgnoreCase
    } else {
        [System.StringComparison]::Ordinal
    }
    Assert-Condition -Condition ($candidatePath.StartsWith($directoryPrefix, $comparison)) `
        -Message "Chemin de test hors du dossier prive: $candidatePath"
}

function Invoke-FailingBuild {
    param([string[]]$Arguments)
    $savedErrorPreference = $ErrorActionPreference
    $PSNativeCommandUseErrorActionPreference = $false
    try {
        # Capture expected stderr from Windows PowerShell as well as pwsh.
        $ErrorActionPreference = "Continue"
        $capturedOutput = @(& $hostExecutable -NoProfile -ExecutionPolicy Bypass -File $buildScript @Arguments 2>&1)
        $buildExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedErrorPreference
    }
    return [pscustomobject]@{
        ExitCode = $buildExitCode
        Output = ($capturedOutput | ForEach-Object { $_.ToString() }) -join "`n"
    }
}

function Assert-PreservedObject {
    param([string]$ObjectPath, [string]$ExpectedHash, [string]$OutputDirectory)
    Assert-Condition -Condition (Test-Path -LiteralPath $ObjectPath -PathType Leaf) `
        -Message "Le build en echec a supprime l'objet existant."
    $actualHash = (Get-FileHash -LiteralPath $ObjectPath -Algorithm SHA256).Hash
    Assert-Condition -Condition ($actualHash -eq $ExpectedHash) `
        -Message "Le build en echec a modifie l'objet existant."
    $temporaryFiles = @(Get-ChildItem -LiteralPath $OutputDirectory -File -Filter "*.tmp" -Recurse)
    Assert-Condition -Condition ($temporaryFiles.Count -eq 0) `
        -Message "Le build en echec a laisse des objets temporaires."
}

Assert-WithinDirectory -Candidate $privateRoot -Directory $repoRoot
Assert-WithinDirectory -Candidate $runDirectory -Directory $privateRoot
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

try {
    $outputDirectory = Join-Path $runDirectory "output"
    $sdkHeaderDirectory = Join-Path $runDirectory "sdk/include/distingnt"
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $sdkHeaderDirectory -Force | Out-Null

    $objectPath = Join-Path $outputDirectory "preserved.o"
    [System.IO.File]::WriteAllBytes($objectPath, [byte[]](83, 77, 78, 84, 1, 2, 3, 4))
    $expectedHash = (Get-FileHash -LiteralPath $objectPath -Algorithm SHA256).Hash

    $compilerPath = Join-Path $runDirectory "failing_compiler.ps1"
    $compilerMarker = Join-Path $runDirectory "compiler-called.txt"
    $compilerFixture = @'
$ErrorActionPreference = "Stop"
$compilerArguments = [string[]]$args
$outputIndex = [System.Array]::IndexOf($compilerArguments, "-o")
if ($outputIndex -lt 0 -or $outputIndex + 1 -ge $compilerArguments.Length) {
    throw "La fixture compilateur n'a pas recu de chemin de sortie."
}
$partialOutput = $compilerArguments[$outputIndex + 1]
[System.IO.File]::WriteAllText((Join-Path $PSScriptRoot "compiler-called.txt"), $partialOutput)
[System.IO.File]::WriteAllBytes($partialOutput, [byte[]](80, 65, 82, 84, 73, 65, 76))
Write-Output "Intentional compiler failure (37)"
exit 37
'@
    [System.IO.File]::WriteAllText($compilerPath, $compilerFixture)

    $originalHeader = Join-Path $repoRoot "include/distingnt/api.h"
    $mutatedHeader = Join-Path $sdkHeaderDirectory "api.h"
    [System.IO.File]::WriteAllText($mutatedHeader,
        [System.IO.File]::ReadAllText($originalHeader) + "`n// Deliberately modified SDK header.`n")

    $commonArguments = @("-Toolchain", $compilerPath, "-OutDir", $outputDirectory, "-Target", "preserved.o")
    $invalidSdk = Invoke-FailingBuild -Arguments ($commonArguments + @("-NtApiPath", (Join-Path $runDirectory "sdk")))
    Assert-Condition -Condition ($invalidSdk.ExitCode -ne 0) `
        -Message "Le SDK modifie a ete accepte par le build."
    Assert-Condition -Condition ($invalidSdk.Output -match 'SHA-256') `
        -Message "Le build n'a pas signale le rejet du hash SDK: $($invalidSdk.Output)"
    Assert-Condition -Condition (!(Test-Path -LiteralPath $compilerMarker)) `
        -Message "Le compilateur a ete lance avant le rejet du SDK modifie."
    Assert-PreservedObject -ObjectPath $objectPath -ExpectedHash $expectedHash -OutputDirectory $outputDirectory
    Write-Host "PASS: SDK modifie rejete avant compilation; objet existant preserve."

    $failedCompilation = Invoke-FailingBuild -Arguments $commonArguments
    Assert-Condition -Condition ($failedCompilation.ExitCode -ne 0) `
        -Message "Le build a ignore l'echec du compilateur."
    Assert-Condition -Condition (Test-Path -LiteralPath $compilerMarker -PathType Leaf) `
        -Message "La fixture compilateur n'a pas ete executee: $($failedCompilation.Output)"
    Assert-Condition -Condition ($failedCompilation.Output -match 'Intentional compiler failure \(37\)') `
        -Message "L'echec attendu du compilateur est absent: $($failedCompilation.Output)"
    $partialOutputPath = [System.IO.File]::ReadAllText($compilerMarker)
    Assert-WithinDirectory -Candidate $partialOutputPath -Directory $outputDirectory
    Assert-Condition -Condition (!(Test-Path -LiteralPath $partialOutputPath)) `
        -Message "L'objet partiel du compilateur n'a pas ete nettoye."
    Assert-PreservedObject -ObjectPath $objectPath -ExpectedHash $expectedHash -OutputDirectory $outputDirectory
    Write-Host "PASS: echec compilateur propage; objet existant preserve et temporaire nettoye."
} finally {
    Assert-WithinDirectory -Candidate $runDirectory -Directory $privateRoot
    Assert-WithinDirectory -Candidate $privateRoot -Directory $repoRoot
    if (Test-Path -LiteralPath $runDirectory -PathType Container) {
        Remove-Item -LiteralPath $runDirectory -Recurse -Force
    }
}

exit 0
