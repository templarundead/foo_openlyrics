param (
    [Parameter(Mandatory)]
    [string]$output_path
)

$ErrorActionPreference = 'Stop'
# A fork or a fresh checkout may not have any version tags. That is a valid
# development build, but it must still get a header for Release compilation.
$version_string = git describe --tags --first-parent HEAD 2>$null
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrEmpty($version_string)) {
    $commit = git rev-parse --short=12 HEAD
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrEmpty($commit)) {
        throw 'Failed to compute a version: neither a version tag nor HEAD is available.'
    }
    $version_string = "0.0-alpha-g$commit"
} elseif ($version_string.StartsWith('v')) {
    $version_string = $version_string.Substring(1) # Remove the leading 'v'.
} else {
    throw "Commit description '$version_string' does not have the expected v-prefixed format."
}

Write-Output "Computing version header for '$version_string'..."
$output_dir = Split-Path -Parent $output_path
if (-not [string]::IsNullOrEmpty($output_dir)) {
    New-Item -ItemType 'directory' -Force $output_dir | Out-Null
}
Set-Content -LiteralPath $output_path -Encoding utf8 -Value "#pragma once`n`n#define OPENLYRICS_VERSION `"$version_string`""
Write-Output "Version header saved to $output_path"
