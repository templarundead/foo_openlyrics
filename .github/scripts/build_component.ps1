param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,

    [Parameter(Mandatory = $true)]
    [ValidateSet('x86', 'x64')]
    [string]$Platform
)

# Retain normal MSBuild output and its exit status, but also publish compiler
# errors as check annotations (available even when log downloads are blocked).
& msbuild /r /m "/p:Configuration=$Configuration" "/p:Platform=$Platform" build\foo_openlyrics.sln 2>&1 |
    Tee-Object -Variable buildOutput
$buildExitCode = $LASTEXITCODE

if ($buildExitCode -ne 0) {
    $errors = $buildOutput | ForEach-Object { "$_" } |
        Where-Object { $_ -match '\b(?:fatal )?error(?: [A-Z]+\d+)?\s*:' } |
        Select-Object -Unique
    foreach ($message in $errors) {
        $escaped = $message.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
        Write-Host "::error::$escaped"
    }
}
exit $buildExitCode
