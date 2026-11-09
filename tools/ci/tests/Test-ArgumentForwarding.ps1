#Requires -Version 7.6

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$temporary = [IO.Path]::GetFullPath((Join-Path $temporaryRoot ([guid]::NewGuid().ToString('N'))))
$temporaryRootPrefix = $temporaryRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (-not $temporary.StartsWith($temporaryRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The argument test temporary directory escaped the system temp root.'
}
New-Item -Path $temporary -ItemType Directory | Out-Null
try {
    $echoCli = Join-Path $temporary 'echo-arguments.ps1'
    $source = 'param([string[]] $Arguments); Write-Output (ConvertTo-Json -InputObject $Arguments -Compress)'
    [IO.File]::WriteAllText($echoCli, $source, [Text.UTF8Encoding]::new($false))
    $expectedArguments = @(
        'native',
        '--root',
        'C:\build folder\a "quoted" path\$(literal)',
        '--value',
        'literal $() ; *?[]'
    )
    $pwsh = Join-Path $PSHOME 'pwsh.exe'
    $command = '$cli = $args[0]; $cliArguments = [string[]] @($args | Select-Object -Skip 1); & $cli -Arguments $cliArguments; exit $LASTEXITCODE'
    $output = @(& $pwsh -NoLogo -NoProfile -CommandWithArgs $command $echoCli @expectedArguments)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Argument-forwarding child exited with $exitCode."
    }
    $actual = $output -join "`n"
    $expected = ConvertTo-Json -InputObject ([string[]] $expectedArguments) -Compress
    if ($actual -cne $expected) {
        throw "Forwarded arguments differed. Expected $expected; got $actual."
    }
} finally {
    Remove-Item -LiteralPath $temporary -Recurse -Force
}

Write-Output 'PowerShell CI argument forwarding passed.'
