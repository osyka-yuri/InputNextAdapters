#Requires -Version 7.6

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Cli,
    [Parameter(Mandatory = $true)][string] $Toolset,
    [Parameter(Mandatory = $true)][string] $Compiler,
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'x86')][string] $Architecture,
    [Parameter(Mandatory = $true)][string[]] $Arguments
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$cliPath = (Resolve-Path -LiteralPath $Cli).Path
$toolsetPath = (Resolve-Path -LiteralPath $Toolset).Path
$compilerReportPath = (Resolve-Path -LiteralPath $Compiler).Path
$toolsetReport = [IO.File]::ReadAllText($toolsetPath) | ConvertFrom-Json -AsHashtable
$compilerReport = [IO.File]::ReadAllText($compilerReportPath) | ConvertFrom-Json -AsHashtable
if ($toolsetReport.schema_version -ne 1 -or $toolsetReport.platform -ne 'windows-x64' -or
    $compilerReport.schema_version -ne 1 -or $compilerReport.family -ne 'production') {
    throw 'The verified Windows toolchain reports are unsupported.'
}

$installationRoot = [string] $compilerReport.installation_root
$toolsetVersion = [string] $compilerReport.vcvars_version
$devShell = Join-Path $installationRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
Import-Module -Name $devShell -Force
Enter-VsDevShell -VsInstallPath $installationRoot -SkipAutomaticLocation `
    -DevCmdArguments "-arch=$Architecture -host_arch=x64 -vcvars_ver=$toolsetVersion"

foreach ($variable in @('PATH', 'PSModulePath')) {
    if ($toolsetReport.environment.ContainsKey($variable)) {
        $locked = [string] $toolsetReport.environment[$variable]
        $current = [Environment]::GetEnvironmentVariable($variable, 'Process')
        [Environment]::SetEnvironmentVariable($variable, "$locked$([IO.Path]::PathSeparator)$current", 'Process')
    }
}
$compilerPath = if ($Architecture -eq 'x64') {
    [string] $compilerReport.cl_path
} else {
    Join-Path ([string] $compilerReport.toolset_root) 'bin/Hostx64/x86/cl.exe'
}
$compilerPath = [IO.Path]::GetFullPath($compilerPath)
$compilerDirectory = Split-Path -Parent $compilerPath
$currentPath = [Environment]::GetEnvironmentVariable('PATH', 'Process')
[Environment]::SetEnvironmentVariable(
    'PATH',
    "$compilerDirectory$([IO.Path]::PathSeparator)$currentPath",
    'Process'
)
$activeCompiler = Get-Command -Name 'cl.exe' -CommandType Application -ErrorAction Stop |
    Select-Object -First 1
$actualCompilerPath = [IO.Path]::GetFullPath($activeCompiler.Source)
$actualVersion = [string] (Get-Item -LiteralPath $actualCompilerPath).VersionInfo.FileVersion
if ($actualCompilerPath -ine $compilerPath -or $actualVersion -cne [string] $compilerReport.cl_version) {
    throw 'The selected MSVC compiler does not match the verified compiler report.'
}

& $cliPath @Arguments
exit $LASTEXITCODE
