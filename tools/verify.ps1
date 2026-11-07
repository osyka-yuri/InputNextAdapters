#Requires -Version 7.6

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Preset,

    [ValidateSet('foundation', 'skyrim-se', 'skyrim-le', 'fallout4')]
    [string] $Project = 'foundation',

    [ValidateRange(1, 256)]
    [int] $Jobs = [Environment]::ProcessorCount
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false

function Get-ApplicationPath {
    param([Parameter(Mandatory = $true)][string] $Name)

    $command = Get-Command -Name $Name -CommandType Application -ErrorAction Stop |
        Select-Object -First 1
    return $command.Source
}

function Invoke-LoggedStep {
    param(
        [Parameter(Mandatory = $true)][string] $Executable,
        [Parameter(Mandatory = $true)][string[]] $Arguments,
        [Parameter(Mandatory = $true)][string] $LogPath
    )

    $argumentText = ConvertTo-Json -InputObject @($Arguments) -Compress
    [IO.File]::WriteAllText(
        $LogPath,
        "Executable: $Executable`nArguments: $argumentText`n`n",
        [Text.UTF8Encoding]::new($false)
    )

    & $Executable @Arguments 2>&1 | Tee-Object -FilePath $LogPath -Append
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $failure = [InvalidOperationException]::new("Command failed with exit code ${exitCode}: $Executable")
        $failure.Data['ExitCode'] = $exitCode
        $failure.Data['LogPath'] = $LogPath
        throw $failure
    }
}

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$projectRoot = switch ($Project) {
    'foundation' { $repositoryRoot }
    'skyrim-se' { Join-Path $repositoryRoot 'adapters/skyrim_se' }
    'skyrim-le' { Join-Path $repositoryRoot 'adapters/skyrim_le' }
    'fallout4' { Join-Path $repositoryRoot 'adapters/fallout4' }
}
$presetFile = Join-Path $projectRoot 'CMakePresets.json'
if (-not (Test-Path -LiteralPath $presetFile -PathType Leaf)) {
    throw "CMake presets were not found for project '$Project': $presetFile"
}

$presetDocument = Get-Content -LiteralPath $presetFile -Raw | ConvertFrom-Json
$buildPreset = @($presetDocument.buildPresets | Where-Object { $_.name -ceq $Preset }) |
    Select-Object -First 1
$testPreset = @($presetDocument.testPresets | Where-Object { $_.name -ceq $Preset }) |
    Select-Object -First 1
$configureName = if ($buildPreset) { [string] $buildPreset.configurePreset } else { '' }
$configurePreset = @($presetDocument.configurePresets | Where-Object { $_.name -ceq $configureName }) |
    Select-Object -First 1

if (-not $buildPreset -or -not $testPreset -or -not $configurePreset -or
    $testPreset.configurePreset -cne $configureName) {
    $availablePresets = @(
        $presetDocument.testPresets |
            Where-Object { $null -ne $_.configurePreset } |
            ForEach-Object { $_.name }
    ) -join ', '
    throw "Preset '$Preset' is not a configure/build/test preset for '$Project'. Available verification presets: $availablePresets"
}

$cmake = Get-ApplicationPath -Name 'cmake'
$ctest = Get-ApplicationPath -Name 'ctest'
$null = Get-ApplicationPath -Name 'ninja'
$runId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), ([guid]::NewGuid().ToString('N').Substring(0, 8))
$safePreset = [regex]::Replace($Preset, '[^A-Za-z0-9_.-]', '_')
$logDirectory = Join-Path $repositoryRoot "build/verification/$Project/$safePreset/$runId"
$null = New-Item -ItemType Directory -Path $logDirectory -Force
$junitPath = Join-Path $logDirectory 'tests.junit.xml'
$exitCode = 0

Write-Information "Verification logs: $logDirectory" -InformationAction Continue
Push-Location -LiteralPath $projectRoot
try {
    Invoke-LoggedStep -Executable $cmake -Arguments @('--preset', $configureName) `
        -LogPath (Join-Path $logDirectory 'configure.log')
    Invoke-LoggedStep -Executable $cmake -Arguments @('--build', '--preset', $Preset, '--parallel', "$Jobs") `
        -LogPath (Join-Path $logDirectory 'build.log')
    Invoke-LoggedStep -Executable $ctest -Arguments @('--preset', $Preset, '--output-junit', $junitPath) `
        -LogPath (Join-Path $logDirectory 'test.log')
} catch {
    if ($_.Exception.Data.Contains('ExitCode')) {
        $exitCode = [int] $_.Exception.Data['ExitCode']
    } else {
        $exitCode = 1
    }

    $logPath = [string] $_.Exception.Data['LogPath']
    $failureMessage = $_.Exception.Message
    if ($logPath) {
        $failureMessage += " Log: $logPath"
    }
    Write-Error -Message $failureMessage -ErrorAction Continue
} finally {
    Pop-Location
}

if ($exitCode -eq 0) {
    Write-Information "CTest JUnit: $junitPath" -InformationAction Continue
} else {
    exit $exitCode
}
