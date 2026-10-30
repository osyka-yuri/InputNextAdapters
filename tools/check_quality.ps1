#Requires -Version 7.6
#Requires -Modules @{ ModuleName = 'PSScriptAnalyzer'; ModuleVersion = '1.25.0' }

[CmdletBinding()]
param(
    [switch] $ApplyFormat
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
Set-StrictMode -Version Latest

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$expectedLlvm = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'llvm-version.txt')).Trim()
$excludedDirectories = @(
    '.git', '.idea', '.vs', 'CMakeFiles', 'cmakefiles', 'debug', 'generated',
    'obj', 'out', 'packages', 'release', 'scratch', 'third_party', 'vendor',
    'win32', 'x64'
)

function Get-RepositoryFile {
    param(
        [Parameter(Mandatory = $true)][string[]] $Roots,
        [Parameter(Mandatory = $true)][string[]] $Extensions
    )

    $files = [Collections.Generic.List[IO.FileInfo]]::new()
    foreach ($root in $Roots) {
        $rootPath = Join-Path $repositoryRoot $root
        if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) {
            continue
        }
        $rootItem = Get-Item -LiteralPath $rootPath -Force -ErrorAction Stop
        if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            continue
        }

        $pending = [Collections.Generic.Stack[string]]::new()
        $pending.Push($rootPath)
        while ($pending.Count -gt 0) {
            $directory = $pending.Pop()
            foreach ($entry in Get-ChildItem -LiteralPath $directory -Force -File:$false -ErrorAction Stop) {
                if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                    continue
                }
                if ($entry.PSIsContainer) {
                    if ($entry.Name -in $excludedDirectories -or $entry.Name -match '^build($|[-_])') {
                        continue
                    }
                    $pending.Push($entry.FullName)
                } elseif ($Extensions -contains $entry.Extension.ToLowerInvariant()) {
                    $files.Add($entry)
                }
            }
        }
    }

    return $files.ToArray() | Sort-Object -Property FullName -Unique
}

$powerShellFiles = @(Get-RepositoryFile -Roots @('tools') -Extensions @('.ps1', '.psm1', '.psd1'))
foreach ($file in $powerShellFiles) {
    $source = [IO.File]::ReadAllText($file.FullName)
    $lineEnding = if ($source.Contains("`r`n")) { "`r`n" } else { "`n" }
    $normalizedSource = $source.Replace("`r`n", "`n").Replace("`r", "`n")
    $formattedSource = (Invoke-Formatter -ScriptDefinition $normalizedSource -Settings CodeFormattingOTBS).
    Replace("`r`n", "`n").Replace("`r", "`n")

    if ($normalizedSource -cne $formattedSource) {
        if ($ApplyFormat) {
            $formattedSource = $formattedSource.Replace("`n", $lineEnding)
            [IO.File]::WriteAllText($file.FullName, $formattedSource, [Text.UTF8Encoding]::new($false))
        } else {
            throw "PowerShell formatting differs: $($file.FullName). Use -ApplyFormat to format owned scripts."
        }
    }

    $diagnostics = @(Invoke-ScriptAnalyzer -Path $file.FullName)
    if ($diagnostics.Count -gt 0) {
        $diagnostics |
            Format-Table -Property ScriptName, Line, RuleName, Message -AutoSize |
            Out-String |
            Write-Information -InformationAction Continue
        throw "PowerShell analysis failed: $($file.FullName)"
    }
}

$clangFormatCommand = Get-Command -Name 'clang-format.exe' -CommandType Application -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $clangFormatCommand) {
    $clangFormatCommand = Get-Command -Name 'clang-format' -CommandType Application -ErrorAction Stop |
        Select-Object -First 1
}
$clangFormat = $clangFormatCommand.Source
$versionOutput = @(& $clangFormat --version 2>&1)
$versionExitCode = $LASTEXITCODE
$versionText = ($versionOutput | ForEach-Object { [string] $_ }) -join "`n"
if ($versionExitCode -ne 0 -or $versionText -notmatch "(?<![0-9])$([regex]::Escape($expectedLlvm))(?![0-9.])") {
    throw "Expected clang-format $expectedLlvm, got: $versionText"
}

$cppFiles = @(
    Get-RepositoryFile -Roots @('adapters', 'include', 'src', 'tests', 'examples', 'tools') `
        -Extensions @('.c', '.cc', '.cpp', '.cxx', '.c++', '.h', '.hh', '.hpp', '.hxx', '.inl', '.ipp', '.tpp')
)
if ($cppFiles.Count -eq 0) {
    throw 'No owned C/C++ source files were found to format.'
}

$fileListPath = Join-Path ([IO.Path]::GetTempPath()) "inputnext-adapters-format-$([guid]::NewGuid().ToString('N')).txt"
try {
    [string[]] $paths = $cppFiles.FullName
    [IO.File]::WriteAllLines($fileListPath, $paths, [Text.UTF8Encoding]::new($false))

    $formatterArguments = @('--style=file', '--fallback-style=none', '--fail-on-incomplete-format')
    if ($ApplyFormat) {
        $formatterArguments += '-i'
    } else {
        $formatterArguments += @('--dry-run', '--Werror')
    }
    $formatterArguments += "--files=$fileListPath"

    Write-Information "clang-format ${expectedLlvm}: $($cppFiles.Count) owned C/C++ files" -InformationAction Continue
    & $clangFormat @formatterArguments
    $formatExitCode = $LASTEXITCODE
    if ($formatExitCode -ne 0) {
        throw "clang-format failed with exit code ${formatExitCode}."
    }
} finally {
    Remove-Item -LiteralPath $fileListPath -Force -ErrorAction SilentlyContinue
}

Write-Information 'PowerShell analysis, PowerShell formatting, and C/C++ formatting passed.' -InformationAction Continue
