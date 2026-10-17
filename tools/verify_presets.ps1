[CmdletBinding()]
param(
    [string]$Compiler = "",
    [string]$Python = "",
    [string]$BankDir = ".\presets\WideSmearNT16",
    [string]$OutDir = ".\build\preset-validation"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-Executable {
    param([string]$Requested, [string[]]$Candidates, [string]$Label)
    if ($Requested) {
        if (Test-Path -LiteralPath $Requested -PathType Leaf) {
            return (Resolve-Path -LiteralPath $Requested).Path
        }
        $requestedCommand = Get-Command $Requested -CommandType Application -ErrorAction SilentlyContinue
        if ($requestedCommand) { return $requestedCommand.Source }
        throw "$Label introuvable: $Requested"
    }
    foreach ($candidate in $Candidates) {
        $candidateCommand = Get-Command $candidate -CommandType Application -ErrorAction SilentlyContinue
        if ($candidateCommand) { return $candidateCommand.Source }
    }
    throw "$Label introuvable. Passe son chemin explicitement."
}

function Resolve-ProjectPath {
    param([string]$Requested)
    if ([System.IO.Path]::IsPathRooted($Requested)) {
        return [System.IO.Path]::GetFullPath($Requested)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Requested))
}

$nativeCompiler = Resolve-Executable -Requested $Compiler -Candidates @("clang++", "g++") -Label "Compilateur C++"
$pythonExecutable = Resolve-Executable -Requested $Python -Candidates @("python", "python3", "py") -Label "Python 3"
$outputDirectory = Resolve-ProjectPath -Requested $OutDir
$bankDirectory = Resolve-ProjectPath -Requested $BankDir
$isWindowsHost = [System.Environment]::OSVersion.Platform -eq [System.PlatformID]::Win32NT
$executableName = if ($isWindowsHost) { "inspect_widesmearnt.exe" } else { "inspect_widesmearnt" }
$executablePath = Join-Path $outputDirectory $executableName
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$sources = @(
    "tests\PresetInspector.cpp",
    "src\WideSmearNTAdapter.cpp",
    "src\NTHostShim.cpp",
    "src\widesmearnt_core\SmearCore.cpp",
    "src\widesmearnt_core\AllpassNetwork.cpp",
    "src\widesmearnt_core\AllpassSection.cpp",
    "src\widesmearnt_core\ParamSmoother.cpp"
) | ForEach-Object { Join-Path $repoRoot $_ }
$compilerArguments = @(
    "-std=c++14", "-O2", "-Wall", "-Wextra", "-Werror",
    "-I$(Join-Path $repoRoot 'include')", "-I$(Join-Path $repoRoot 'src')"
) + $sources + @("-o", $executablePath)

Write-Host "Compilation de l'inspecteur natif: $executablePath"
& $nativeCompiler @compilerArguments
if ($LASTEXITCODE -ne 0) { throw "Compilation de l'inspecteur echouee: $LASTEXITCODE" }

$pythonArguments = @()
if ([System.IO.Path]::GetFileNameWithoutExtension($pythonExecutable) -eq "py") {
    $pythonArguments += "-3"
}
$pythonArguments += @(
    (Join-Path $PSScriptRoot "verify_presets.py"),
    "--inspector", $executablePath,
    "--bank", $bankDirectory,
    "--output", (Join-Path $outputDirectory "validation.json")
)
& $pythonExecutable @pythonArguments
if ($LASTEXITCODE -ne 0) { throw "Validation des presets echouee: $LASTEXITCODE" }
