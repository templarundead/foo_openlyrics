$ErrorActionPreference = 'Stop'
trap {
    $message = ($_.Exception.Message + "`n" + $_.ScriptStackTrace).Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
    Write-Host "::error::$message"
    throw
}
$global:OpenLyricsVersionHeaderTestState = @{}
$generator = Join-Path $PSScriptRoot '../../build/generate_version_header.ps1'
$tempDirectory = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
$header = Join-Path $tempDirectory 'path with spaces/openlyrics_version_generated.h'

# Replace only git in this script's scope; no tags, commits, or branches are
# created or changed in the checked-out repository.
function git {
    if ($args[0] -eq 'describe') {
        $global:LASTEXITCODE = $global:OpenLyricsVersionHeaderTestState.describeExitCode
        return $global:OpenLyricsVersionHeaderTestState.description
    }
    if ($args[0] -eq 'rev-parse') {
        $global:LASTEXITCODE = $global:OpenLyricsVersionHeaderTestState.commitExitCode
        return $global:OpenLyricsVersionHeaderTestState.commit
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
    $global:OpenLyricsVersionHeaderTestState.commit = '0123456789ab'
    $global:OpenLyricsVersionHeaderTestState.commitExitCode = 0
    $global:OpenLyricsVersionHeaderTestState.describeExitCode = 0
    $global:OpenLyricsVersionHeaderTestState.description = 'v1.13'
    Assert-Header '1.13'
    $global:OpenLyricsVersionHeaderTestState.description = 'v1.13-2-g0123456'
    Assert-Header '1.13-2-g0123456'

    $global:OpenLyricsVersionHeaderTestState.describeExitCode = 128
    $global:OpenLyricsVersionHeaderTestState.description = $null
    Assert-Header '0.0-alpha-g0123456789ab'

    $global:OpenLyricsVersionHeaderTestState.describeExitCode = 0
    $global:OpenLyricsVersionHeaderTestState.description = 'invalid-tag'
    Assert-GenerationFails
    $global:OpenLyricsVersionHeaderTestState.describeExitCode = 128
    $global:OpenLyricsVersionHeaderTestState.description = $null
    $global:OpenLyricsVersionHeaderTestState.commitExitCode = 128
    $global:OpenLyricsVersionHeaderTestState.commit = $null
    Assert-GenerationFails
    Write-Output 'Passed version-header generation tests (tagged, untagged, and invalid inputs).'
} finally {
    Remove-Variable -Name OpenLyricsVersionHeaderTestState -Scope Global
    if (Test-Path -LiteralPath $tempDirectory) {
        Remove-Item -LiteralPath $tempDirectory -Recurse -Force
    }
}
# The final negative case deliberately sets a failing native-command status.
# Do not let GitHub's pwsh epilogue treat that expected failure as a job failure.
$global:LASTEXITCODE = 0
