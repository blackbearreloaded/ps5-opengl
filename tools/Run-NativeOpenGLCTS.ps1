# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Run one frozen PPSA99005 Khronos GL33 CTS shard through the native-title protocol.

[CmdletBinding()]
param(
    [string]$AppDirectory,
    [string]$BoilerplateDirectory,
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedEbootSha256,
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{64}$')]
    [string]$ExpectedArgumentsSha256,
    [ValidatePattern('^$|^[0-9a-fA-F]{64}$')]
    [string]$ExpectedCaseListSha256 = '',
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedCommit,
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedBoilerplateCommit,
    [Parameter(Mandatory)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$ExpectedProtocolCommit,
    [ValidateRange(1, 100000)]
    [int]$ExpectedExecuted = 6,
    [ValidateRange(0, 100000)]
    [int]$MaximumNotSupported = 0,
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Za-z0-9.-]+$')]
    [string]$Ps5Host,
    [ValidatePattern('^$|^[A-Za-z0-9.-]+$')]
    [string]$ChiakiHost = '',
    [ValidatePattern('^$|^[A-Za-z0-9.-]+$')]
    [string]$KlogHost = '',
    [ValidateRange(5, 3600)]
    [int]$ObservationSeconds = 20,
    [string]$FtpCredential = 'anonymous:homebrew',
    [switch]$FirstRegistration,
    [switch]$Incremental,
    [switch]$Headless,
    [switch]$ReuseInstalledBinaries,
    [string]$LockPath,
    [string]$ResultsDirectory
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($AppDirectory)) {
    $AppDirectory = Join-Path $scriptRoot `
        '..\build\native-app\PPSA99005-cts\dist\PPSA99005'
}
if ([string]::IsNullOrWhiteSpace($ResultsDirectory)) {
    $ResultsDirectory = Join-Path $scriptRoot '..\results\native-app-cts'
}

$app = (Resolve-Path -LiteralPath $AppDirectory).Path
$repo = (Resolve-Path -LiteralPath (Join-Path $scriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($LockPath)) {
    $LockPath = Join-Path $repo '..\..\..\lock.txt'
}
if ([string]::IsNullOrWhiteSpace($BoilerplateDirectory)) {
    $BoilerplateDirectory = Join-Path $repo '..\ps5-native-app-boilerplate'
}
$boilerplate = (Resolve-Path -LiteralPath $BoilerplateDirectory).Path
$protocol = (Resolve-Path -LiteralPath (Join-Path $repo `
    '..\..\docs\ps5-homebrew-dev-protocol')).Path
$cycle = Join-Path $protocol 'scripts\Invoke-Ps5Cycle.ps1'
$eboot = Join-Path $app 'eboot.bin'
$arguments = Join-Path $app 'cts-args.txt'
$caseList = Join-Path $app 'cts-shard.txt'
$downloadData = Join-Path ([IO.Path]::GetTempPath()) `
    "pss-opengl-cts-download0-$PID-$([Guid]::NewGuid().ToString('N')).dat"
$extractDirectory = Join-Path ([IO.Path]::GetTempPath()) `
    "pss-opengl-cts-receipt-$PID-$([Guid]::NewGuid().ToString('N'))"
$resultFile = $null
$postHealthChecked = $false
$lockReleased = $false
if ($ReuseInstalledBinaries -and (-not $Incremental -or $FirstRegistration)) {
    throw 'Reusing installed binaries requires Incremental and an existing registration.'
}

foreach ($required in @($eboot, $arguments, (Join-Path $app 'sce_module\libc.prx'),
        (Join-Path $app 'sce_sys\param.json'), $cycle)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required native CTS artifact is missing: $required"
    }
}

function Assert-Hash([string]$Path, [string]$Expected) {
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actual -ne $Expected) {
        throw "SHA-256 mismatch for $Path`: expected $Expected, built $actual"
    }
}

function Assert-InstalledBinary([string]$RelativePath) {
    $probe = [IO.Path]::GetTempFileName()
    try {
        $probeWsl = (wsl.exe wslpath -a -- ($probe -replace '\\', '/')).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $probeWsl) { throw 'Could not resolve remote hash probe.' }
        # ftpsrv defaults to an ELF view; SELF toggles it off for this connection.
        & wsl.exe -e curl --fail --silent --show-error --disable-epsv --quote SELF `
            --connect-timeout 5 --max-time 90 -u $FtpCredential `
            "ftp://${Ps5Host}:2121/data/homebrew/PPSA99005/$RelativePath" --output $probeWsl
        if ($LASTEXITCODE -ne 0) { throw "Could not verify installed $RelativePath." }
        $expected = (Get-FileHash -LiteralPath (Join-Path $app $RelativePath) -Algorithm SHA256).Hash
        $actual = (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash
        if ($actual -ne $expected) {
            New-Item -ItemType Directory -Path $ResultsDirectory -Force | Out-Null
            $mismatch = Join-Path $ResultsDirectory ("installed-$actual-" + [IO.Path]::GetFileName($RelativePath))
            Copy-Item -LiteralPath $probe -Destination $mismatch
            Write-Warning "Preserved installed binary mismatch: $mismatch"
        }
        Assert-Hash $probe $expected
        Write-Host "REMOTE_BINARY_VERIFIED $RelativePath sha256=$expected"
    } finally {
        Remove-Item -LiteralPath $probe -Force
    }
}

function Test-TcpPort([int]$Port) {
    & wsl.exe -e nc -z -w 3 $Ps5Host $Port
    return $LASTEXITCODE -eq 0
}

function Assert-ServiceHealth([string]$Phase) {
    $closed = @(2121, 3232, 9021 | Where-Object { -not (Test-TcpPort $_) })
    if ($closed.Count -ne 0) {
        throw "$Phase PS5 service check failed; closed ports: $($closed -join ', ')"
    }
    Write-Host "PS5_SERVICES_HEALTHY phase=$Phase host=$Ps5Host"
}

Assert-Hash $eboot $ExpectedEbootSha256
Assert-Hash $arguments $ExpectedArgumentsSha256
$argumentText = Get-Content -LiteralPath $arguments -Raw
if ($argumentText -match '--deqp-caselist-file=/app0/cts-shard\.txt') {
    if (-not $ExpectedCaseListSha256) {
        throw 'A CTS shard requires ExpectedCaseListSha256.'
    }
    if (-not (Test-Path -LiteralPath $caseList -PathType Leaf)) {
        throw "CTS shard list is missing: $caseList"
    }
    Assert-Hash $caseList $ExpectedCaseListSha256
    $selectedCases = @(Get-Content -LiteralPath $caseList |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($selectedCases.Count -ne $ExpectedExecuted) {
        throw "CTS shard contains $($selectedCases.Count) cases; expected $ExpectedExecuted."
    }
} elseif ($ExpectedCaseListSha256) {
    throw 'ExpectedCaseListSha256 was supplied without the CTS shard selector.'
}
$actualCommit = (& git -c "safe.directory=$repo" -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $ExpectedCommit) {
    throw "Commit mismatch: expected $ExpectedCommit, checkout $actualCommit"
}
$actualBoilerplateCommit = (& git -c "safe.directory=$boilerplate" `
    -C $boilerplate rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or
    $actualBoilerplateCommit -ne $ExpectedBoilerplateCommit) {
    throw "Boilerplate commit mismatch: expected $ExpectedBoilerplateCommit, checkout $actualBoilerplateCommit"
}
$actualProtocolCommit = (& git -c "safe.directory=$protocol" `
    -C $protocol rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualProtocolCommit -ne $ExpectedProtocolCommit) {
    throw "Protocol commit mismatch: expected $ExpectedProtocolCommit, checkout $actualProtocolCommit"
}
$dirtyProtocol = @(& git -c "safe.directory=$protocol" -C $protocol `
    status --porcelain)
if ($LASTEXITCODE -ne 0 -or $dirtyProtocol.Count -ne 0) {
    throw 'The hardware protocol repository must be clean before a cycle.'
}
$dirtyConverter = @(& git -c "safe.directory=$boilerplate" -C $boilerplate `
    status --porcelain -- tooling/native/sce_module_writer.cpp)
if ($LASTEXITCODE -ne 0 -or $dirtyConverter.Count -ne 0) {
    throw 'The boilerplate native converter must be clean before a hardware cycle.'
}
$dirty = @(& git -c "safe.directory=$repo" -C $repo status --porcelain)
if ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0) {
    throw 'The OpenGL repository must be clean before a hardware cycle.'
}

$ps5Lock = [IO.Path]::GetFullPath($LockPath)
$lockToken = 'pss-opengl-cts-pid{0}-{1}' -f $PID,
    [Guid]::NewGuid().ToString('N').Substring(0, 8)
$handle = $null
for ($attempt = 0; $attempt -lt 4; ++$attempt) {
    try {
        $handle = [IO.File]::Open($ps5Lock, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::None)
        break
    } catch [IO.IOException] {
        if (Test-Path -LiteralPath $ps5Lock) {
            $owner = (Get-Content -LiteralPath $ps5Lock -Raw).Trim()
            if ($attempt -eq 3) {
                throw "PS5 lock remained occupied: $owner"
            }
            Write-Host "LOCK_WAIT owner=$owner seconds=15"
            Start-Sleep -Seconds 15
            continue
        }
        if ($attempt -eq 3) { throw 'PS5 lock kept changing during acquisition.' }
        Start-Sleep -Milliseconds 100
    }
}
try {
    $bytes = [Text.Encoding]::UTF8.GetBytes($lockToken)
    $handle.Write($bytes, 0, $bytes.Length)
} finally {
    $handle.Dispose()
}

try {
    Write-Host "LOCK_ACQUIRED $lockToken"
    Assert-ServiceHealth 'preflight'
    & (Join-Path $scriptRoot 'Assert-Ps5ForegroundIdle.ps1') `
        -ProtocolDirectory $protocol -Ps5Host $Ps5Host -FtpCredential $FtpCredential `
        -LockPath $ps5Lock -LockToken $lockToken -ResultsDirectory $ResultsDirectory
    if ($ReuseInstalledBinaries) {
        # FTP readback proves identity; do not trust a local deployment stamp.
        Assert-InstalledBinary 'eboot.bin'
        Assert-InstalledBinary 'sce_module/libc.prx'
    }
    New-Item -ItemType Directory -Path $ResultsDirectory -Force | Out-Null
    $cycleArguments = @{
        TitleId = 'PPSA99005'
        AppDirectory = $app
        PreviousTitleId = 'PPSA99005'
        Ps5Host = $Ps5Host
        ChiakiHost = $ChiakiHost
        KlogHost = $KlogHost
        FtpPort = 2121
        KlogPort = 3232
        ElfPort = 9021
        ResultsDirectory = [IO.Path]::GetFullPath($ResultsDirectory)
        ObservationSeconds = $ObservationSeconds
        ObservationStopText = '[pss-opengl-cts] finished'
        FtpCredential = $FtpCredential
        SkipVideoReadiness = $true
        SkipRoutineScreenshots = $true
        Headless = [bool]$Headless
    }
    if (-not $FirstRegistration) {
        $cycleArguments.UseExistingFolderRegistration = $true
    }
    if ($Incremental) {
        if ($FirstRegistration) {
            throw 'The first registration cannot use an incremental upload.'
        }
        $cycleArguments.UploadRelativePaths = @(
            'cts-args.txt'
        )
        if (-not $ReuseInstalledBinaries) {
            $cycleArguments.UploadRelativePaths += @('eboot.bin', 'sce_module/libc.prx')
        }
        if ($ExpectedCaseListSha256) {
            $cycleArguments.UploadRelativePaths += 'cts-shard.txt'
        }
    }

    $runStarted = Get-Date
    & $cycle @cycleArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Managed PPSA99005 CTS cycle failed with exit code $LASTEXITCODE."
    }
    $resultFile = Get-ChildItem -LiteralPath $ResultsDirectory `
        -Filter 'PPSA99005-*-result.json' -File |
        Where-Object LastWriteTime -GE $runStarted |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $resultFile) {
        throw 'Managed PPSA99005 CTS cycle produced no result record.'
    }

    $downloadDataWsl = (wsl.exe wslpath -a -- `
        $downloadData.Replace('\', '/')).Trim()
    & wsl.exe -e curl --fail --silent --show-error --disable-epsv `
        --connect-timeout 5 --max-time 180 `
        -u $FtpCredential `
        "ftp://${Ps5Host}:2121/user/download/PPSA99005/download0.dat" `
        --output $downloadDataWsl
    if ($LASTEXITCODE -ne 0 -or
        -not (Test-Path -LiteralPath $downloadData -PathType Leaf)) {
        throw 'Could not retrieve PPSA99005 CTS download0 evidence.'
    }
    Assert-ServiceHealth 'post-run'
    $postHealthChecked = $true
} finally {
    try {
        if (-not $postHealthChecked) {
            try {
                Assert-ServiceHealth 'post-run'
            } catch {
                Write-Warning $_.Exception.Message
            }
        }
    } finally {
        if (Test-Path -LiteralPath $ps5Lock) {
            $currentToken = Get-Content -LiteralPath $ps5Lock -Raw
            if ($currentToken -eq $lockToken) {
                Remove-Item -LiteralPath $ps5Lock -Force
                $lockReleased = $true
                Write-Host "LOCK_RELEASED $lockToken"
            } else {
                Write-Warning 'Lock ownership changed; refusing to remove it.'
            }
        }
    }
}

try {
    New-Item -ItemType Directory -Path $extractDirectory | Out-Null
    $dotnet = (Get-Command dotnet).Source
    $ufs2Runner = & (Join-Path $boilerplate 'tools\setup-ffpkg-tooling.ps1') `
        -Dotnet $dotnet
    foreach ($remotePath in @('/pss-opengl-cts.status',
            '/pss-opengl-cts.qpa', '/pss-opengl.log')) {
        if ($ufs2Runner -match '^/mnt/') {
            $downloadWsl = (wsl.exe wslpath -a -- `
                ($downloadData -replace '\\', '/')).Trim()
            $extractWsl = (wsl.exe wslpath -a -- `
                ($extractDirectory -replace '\\', '/')).Trim()
            & wsl.exe sh $ufs2Runner extract $downloadWsl $extractWsl $remotePath
        } else {
            $previousRollForward = $env:DOTNET_ROLL_FORWARD
            try {
                $env:DOTNET_ROLL_FORWARD = 'Major'
                & $dotnet $ufs2Runner extract $downloadData `
                    $extractDirectory $remotePath
            } finally {
                $env:DOTNET_ROLL_FORWARD = $previousRollForward
            }
        }
        if ($LASTEXITCODE -ne 0) {
            throw "Could not extract CTS receipt $remotePath."
        }
    }

    $prefix = $resultFile.FullName -replace '-result\.json$', ''
    $archivedArguments = "$prefix-cts-args.txt"
    Copy-Item -LiteralPath $arguments -Destination $archivedArguments
    Assert-Hash $archivedArguments $ExpectedArgumentsSha256
    if ($ExpectedCaseListSha256) {
        $archivedCaseList = "$prefix-cts-shard.txt"
        Copy-Item -LiteralPath $caseList -Destination $archivedCaseList
        Assert-Hash $archivedCaseList $ExpectedCaseListSha256
    }
    foreach ($name in @('pss-opengl-cts.status', 'pss-opengl-cts.qpa',
            'pss-opengl.log')) {
        $source = Join-Path $extractDirectory $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "PPSA99005 produced no $name receipt."
        }
        Copy-Item -LiteralPath $source -Destination "$prefix-$name"
    }

    # Save facts before checking functional success, including failed batches.
    @{
        checkoutCommit = $ExpectedCommit
        argumentsSha256 = $ExpectedArgumentsSha256
        caseListSha256 = $ExpectedCaseListSha256
        ps5Host = $Ps5Host
        postHealthChecked = $postHealthChecked
        lockReleased = $lockReleased
        installedBinariesVerified = [bool]$ReuseInstalledBinaries
    } | ConvertTo-Json | Set-Content -LiteralPath "$prefix-runner.json" -Encoding UTF8

    $statusPath = "$prefix-pss-opengl-cts.status"
    $status = (Get-Content -LiteralPath $statusPath -Raw).Trim()
    if ($status -notmatch '^state=passed complete=1 executed=(\d+) passed=(\d+) failed=0 not_supported=(\d+) warnings=\d+ waived=\d+ device_lost=0$') {
        throw "CTS shard failed; inspect $statusPath`: $status"
    }
    $executed = [int]$Matches[1]
    $passed = [int]$Matches[2]
    $notSupported = [int]$Matches[3]
    if ($executed -ne $ExpectedExecuted -or
        $passed + $notSupported -gt $ExpectedExecuted -or
        $notSupported -gt $MaximumNotSupported) {
        throw "CTS shard count mismatch in $statusPath`: $status"
    }

    $qpaPath = "$prefix-pss-opengl-cts.qpa"
    $qpaParser = Join-Path $repo 'tools\summarize-cts-qpa.py'
    $qpaWsl = (wsl.exe wslpath -a -- ($qpaPath -replace '\\', '/')).Trim()
    $parserWsl = (wsl.exe wslpath -a -- ($qpaParser -replace '\\', '/')).Trim()
    $qpaArguments = @($parserWsl, $qpaWsl, '--json')
    if ($ExpectedCaseListSha256) {
        $caseListWsl = (wsl.exe wslpath -a -- ($caseList -replace '\\', '/')).Trim()
        $qpaArguments += @('--expected-list', $caseListWsl)
    }
    $qpaSummaryText = @(& wsl.exe -e python3 @qpaArguments)
    if ($LASTEXITCODE -ne 0) {
        throw "CTS QPA validation failed; inspect $qpaPath."
    }
    $qpaSummary = ($qpaSummaryText -join "`n") | ConvertFrom-Json
    if (-not $qpaSummary.complete -or $qpaSummary.executed -ne $ExpectedExecuted) {
        throw "CTS QPA count mismatch in $qpaPath."
    }

    $result = Get-Content -LiteralPath $resultFile.FullName -Raw |
        ConvertFrom-Json
    if ($result.outcome -ne 'entered-eboot') {
        throw "CTS lifecycle outcome was $($result.outcome)."
    }
    if ($result.teardownSignal -ne 'runtime-layers-released' -or
        -not $postHealthChecked -or -not $lockReleased) {
        throw 'CTS lifecycle did not complete cleanly.'
    }
    Write-Host "OPENGL_CTS_PASSED executed=$ExpectedExecuted qpa=$prefix-pss-opengl-cts.qpa"
} finally {
    if (Test-Path -LiteralPath $downloadData) {
        Remove-Item -LiteralPath $downloadData -Force
    }
    if (Test-Path -LiteralPath $extractDirectory) {
        $resolved = [IO.Path]::GetFullPath($extractDirectory)
        if (-not $resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()))) {
            throw 'Refusing cleanup outside the temporary directory.'
        }
        Remove-Item -LiteralPath $extractDirectory -Recurse -Force
    }
}
