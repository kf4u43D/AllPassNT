[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path,
    [Parameter(Mandatory = $true)]
    [int]$ExpectedSampleRate,
    [Parameter(Mandatory = $true)]
    [ValidateSet(16, 24)]
    [int]$ExpectedBitDepth,
    [ValidateRange(1, [int]::MaxValue)]
    [int]$ExpectedFrames = 0
)

$ErrorActionPreference = "Stop"
$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$bytes = [System.IO.File]::ReadAllBytes($resolvedPath)
if ($bytes.Length -lt 44) { throw "WAV trop court: $resolvedPath" }

$ascii = [System.Text.Encoding]::ASCII
if ($ascii.GetString($bytes, 0, 4) -ne "RIFF" -or
    $ascii.GetString($bytes, 8, 4) -ne "WAVE" -or
    $ascii.GetString($bytes, 12, 4) -ne "fmt " -or
    $ascii.GetString($bytes, 36, 4) -ne "data") {
    throw "Structure RIFF/WAV invalide: $resolvedPath"
}

$riffSize = [System.BitConverter]::ToUInt32($bytes, 4)
$formatSize = [System.BitConverter]::ToUInt32($bytes, 16)
$format = [System.BitConverter]::ToUInt16($bytes, 20)
$channels = [System.BitConverter]::ToUInt16($bytes, 22)
$sampleRate = [System.BitConverter]::ToUInt32($bytes, 24)
$byteRate = [System.BitConverter]::ToUInt32($bytes, 28)
$blockAlign = [System.BitConverter]::ToUInt16($bytes, 32)
$bitDepth = [System.BitConverter]::ToUInt16($bytes, 34)
$dataSize = [System.BitConverter]::ToUInt32($bytes, 40)
if ($riffSize + 8 -ne $bytes.Length -or $dataSize + 44 -ne $bytes.Length) {
    throw "Tailles RIFF/WAV incoherentes: $resolvedPath"
}
if ($formatSize -ne 16 -or $format -ne 1 -or $channels -ne 2 -or
    $sampleRate -ne $ExpectedSampleRate -or $bitDepth -ne $ExpectedBitDepth) {
    throw "Format PCM inattendu: $resolvedPath"
}
$expectedBlockAlign = 2 * ($ExpectedBitDepth / 8)
$expectedByteRate = $ExpectedSampleRate * $expectedBlockAlign
if ($blockAlign -ne $expectedBlockAlign -or $byteRate -ne $expectedByteRate -or
    $dataSize % $expectedBlockAlign -ne 0) {
    throw "Debit ou alignement PCM incoherent: $resolvedPath"
}
if ($ExpectedFrames -gt 0 -and $dataSize / $expectedBlockAlign -ne $ExpectedFrames) {
    throw "Nombre de frames PCM inattendu: $resolvedPath"
}

$nonZero = $false
for ($i = 44; $i -lt $bytes.Length; ++$i) {
    if ($bytes[$i] -ne 0) {
        $nonZero = $true
        break
    }
}
if (!$nonZero) { throw "Rendu WAV silencieux: $resolvedPath" }
Write-Host "WAV valide: $resolvedPath ($($bytes.Length) octets)"
