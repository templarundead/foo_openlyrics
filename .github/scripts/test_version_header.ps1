$ErrorActionPreference = 'Stop'
$generator = Join-Path $PSScriptRoot '../../build/generate_version_header.ps1'
$tempDirectory = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
$header = Join-Path $tempDirectory 'path with spaces/openlyrics_version_generated.h'

# Replace only git in this script's scope; no tags, commits, or branches are
# created or changed in the checked-out repository.
function git {
    if ($args[0] -eq 'describe') {
        $global:LASTEXITCODE = $script:describeExitCode
        return $script:description
    }
    if ($args[0] -eq 'rev-parse') {
        $global:LASTEXITCODE = $script:commitExitCode
        return $script:commit
    }
    throw "Unexpected git command: $args"
}

function Assert-Header([string]$expected) {
    & $generator -output_path $header | Out-Host
    $content = Get-Content -LiteralPath $header -Raw
    if (-not $content.Contains("#define OPENLYRICS_VERSION `"$expected`"")) {
        throw "Unexpected version header: $content"
    }
}

function Assert-GenerationFails {
    $failed = $false
    try { & $generator -output_path $header | Out-Host }
    catch { $failed = $true }
    if (-not $failed) { throw 'Invalid version inputs must fail generation.' }
}

try {
    $script:commit = '0123456789ab'
    $script:commitExitCode = 0
    $script:describeExitCode = 0
    $script:description = 'v1.13'
    Assert-Header '1.13'
    $script:description = 'v1.13-2-g0123456'
    Assert-Header '1.13-2-g0123456'

    $script:describeExitCode = 128
    $script:description = $null
    Assert-Header '0.0-alpha-g0123456789ab'

    $script:describeExitCode = 0
    $script:description = 'invalid-tag'
    Assert-GenerationFails
    $script:describeExitCode = 128
    $script:description = $null
    $script:commitExitCode = 128
    $script:commit = $null
    Assert-GenerationFails
    Write-Output 'Passed version-header generation tests (tagged, untagged, and invalid inputs).'
} finally {
    if (Test-Path -LiteralPath $tempDirectory) {
        Remove-Item -LiteralPath $tempDirectory -Recurse -Force
    }
}
