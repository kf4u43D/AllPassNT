[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = "Stop"
$expectedSha256 = "2FB0A626EE495CD69D7C2C68AA3EC1206CA7F853E7967AAD8F709FC4613E4206"
if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
    throw "Header API disting NT introuvable: $Path"
}
$actualSha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
if ($actualSha256 -ne $expectedSha256) {
    throw "Header API disting NT non epingle: $Path a le SHA-256 $actualSha256, attendu $expectedSha256 (API v13, commit f440d20)."
}
