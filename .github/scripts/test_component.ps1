param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('x86', 'x64')]
    [string]$Platform
)

$directory = if ($Platform -eq 'x64') { 'build\x64\Debug' } else { 'build\Debug' }
& "$directory\test_foo_openlyrics.exe" "$directory\foo_openlyrics.dll" 2>&1 |
    Tee-Object -Variable testOutput
$testExitCode = $LASTEXITCODE

if ($testExitCode -ne 0) {
    foreach ($line in $testOutput) {
        $message = "$line" -replace '\x1B\[[0-9;]*m', ''
        if ($message -match '^\[FAILED\]|^Unhandled |^Assertion failed ') {
            $escaped = $message.Replace('%', '%25').Replace("`r", '%0D').Replace("`n", '%0A')
            Write-Host "::error::$escaped"
        }
    }
}
exit $testExitCode
