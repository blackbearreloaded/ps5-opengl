# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Run resumable bounded shards of the official Khronos GL 3.3 must-pass list.

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedEbootSha256,
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedCommit,
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedBoilerplateCommit,
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedProtocolCommit,
    [ValidateSet(0, 1, 2, 3)]
    [int[]]$Configuration = @(0),
    [ValidateRange(0, 9885)]
    [int]$StartOffset = 0,
    [ValidateRange(1, 1000)]
    [int]$BatchSize = 1000,
    [string]$Suite = '',
    [ValidateRange(0, 1800)]
    [int]$BudgetSeconds = 180,
    [string]$TimingsPath = '',
    [ValidateRange(0, 100000)]
    [int]$MaximumBatches = 1,
    [ValidateRange(0, 1000)]
    [int]$MaximumNotSupported = 1000,
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Za-z0-9.-]+$')]
    [string]$Ps5Host,
    [ValidateRange(5, 3600)]
    [int]$ObservationSeconds = 60,
    [string]$FtpCredential = 'anonymous:homebrew',
    [switch]$Incremental,
    [switch]$ReuseInstalledBinaries
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = (Resolve-Path -LiteralPath (Join-Path $scriptRoot '..')).Path
$prepare = Join-Path $scriptRoot 'prepare-cts-shard.py'
$runner = Join-Path $scriptRoot 'Run-NativeOpenGLCTS.ps1'
$mustpass = Join-Path $repo `
    'third_party\VK-GL-CTS\external\openglcts\data\gl_cts\data\mustpass\gl\khronos_mustpass\main\gl33-main.txt'
$caseCount = @(Get-Content -LiteralPath $mustpass |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) }).Count
if ($caseCount -ne 9886) {
    throw "Pinned GL33 must-pass list has $caseCount cases; expected 9886."
}
$prepareWsl = (wsl.exe wslpath -a -- ($prepare -replace '\\', '/')).Trim()
if ($LASTEXITCODE -ne 0 -or -not $prepareWsl) {
    throw 'Could not resolve the CTS shard generator in WSL.'
}

$batch = 0
$useIncremental = [bool]$Incremental
$useReuse = [bool]$ReuseInstalledBinaries
if ($BudgetSeconds) {
    if (-not $TimingsPath) { $TimingsPath = Join-Path $repo 'results\native-app-cts\evidence-ledger.json' }
    $timings = (Resolve-Path -LiteralPath $TimingsPath).Path
    $timingsWsl = (wsl.exe wslpath -a -- ($timings -replace '\\', '/')).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $timingsWsl) { throw 'Could not resolve CTS timings.' }
}
foreach ($config in $Configuration) {
    $selectionCount = if ($Suite) { 9886 } else { $caseCount }
    for ($offset = $StartOffset; $offset -lt $selectionCount;
         $offset += $count) {
        if ($MaximumBatches -and $batch -ge $MaximumBatches) {
            Write-Host "CTS_CAMPAIGN_PAUSED batches=$batch"
            return
        }
        $count = [Math]::Min($BatchSize, $selectionCount - $offset)
        $metadata = @{}
        $prepareArguments = @($prepareWsl, '--offset', $offset, '--configuration', $config)
        if ($Suite) { $prepareArguments += @('--suite', $Suite) }
        else { $prepareArguments += @('--count', $count) }
        if ($BudgetSeconds) {
            $prepareArguments += @('--timings', $timingsWsl, '--budget-seconds', $BudgetSeconds)
        }
        $prepared = @(& wsl.exe -e python3 @prepareArguments)
        if ($LASTEXITCODE -ne 0) {
            throw "Could not prepare CTS config $config offset $offset."
        }
        foreach ($line in $prepared) {
            Write-Host $line
            if ($line -match '^([^=]+)=(.*)$') {
                $metadata[$Matches[1]] = $Matches[2]
            }
        }
        foreach ($key in @('arguments_sha256', 'case_list_sha256')) {
            if ($metadata[$key] -notmatch '^[0-9a-f]{64}$') {
                throw "CTS shard generator returned no valid $key."
            }
        }
        $count = [int]$metadata.count
        $selectionCount = [int]$metadata.selection_total
        if ($count -le 0 -or [int]$metadata.next_offset -ne $offset + $count) {
            throw 'CTS selector returned an invalid resume point.'
        }
        $bound = $ObservationSeconds
        if ($BudgetSeconds) {
            $bound = [Math]::Max($bound, [int]$metadata.suggested_observation_seconds)
            if ($bound -gt 3600) { throw 'Selected case needs performance triage: bound exceeds 3600 seconds.' }
        }

        $arguments = @{
            ExpectedEbootSha256 = $ExpectedEbootSha256
            ExpectedArgumentsSha256 = $metadata.arguments_sha256
            ExpectedCaseListSha256 = $metadata.case_list_sha256
            ExpectedCommit = $ExpectedCommit
            ExpectedBoilerplateCommit = $ExpectedBoilerplateCommit
            ExpectedProtocolCommit = $ExpectedProtocolCommit
            ExpectedExecuted = $count
            MaximumNotSupported = [Math]::Min($MaximumNotSupported, $count)
            Ps5Host = $Ps5Host
            ObservationSeconds = $bound
            FtpCredential = $FtpCredential
            ResultsDirectory = Join-Path $repo `
                "results\native-app-cts\config-$config"
        }
        if ($useIncremental) {
            $arguments.Incremental = $true
        }
        if ($useReuse) { $arguments.ReuseInstalledBinaries = $true }
        & $runner @arguments
        $useIncremental = $true
        $useReuse = $true
        ++$batch
        Write-Host "CTS_CAMPAIGN_PROGRESS config=$config next=$($offset + $count) batches=$batch"
    }
}
Write-Host "CTS_CAMPAIGN_COMPLETE configurations=$($Configuration.Count) batches=$batch"
