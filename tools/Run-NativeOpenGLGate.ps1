# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

# Run one frozen PPSA99005 OpenGL gate through the managed native-title protocol.

[CmdletBinding()]
param(
    [string]$AppDirectory,
    [string]$BoilerplateDirectory,
    [Parameter(Mandatory)]
    [ValidatePattern('^egl_public_[A-Za-z0-9_]+\.o$')]
    [string]$ExpectedGate,
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
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Za-z0-9.-]+$')]
    [string]$Ps5Host,
    [ValidateRange(5, 3600)]
    [int]$ObservationSeconds = 15,
    [ValidateLength(0, 256)]
    [string]$ObservationStopText = '',
    [string]$FtpCredential = 'anonymous:homebrew',
    [switch]$FirstRegistration,
    [switch]$Incremental,
    [switch]$Headless,
    [switch]$OwnerConfirmedIdle,
    [string]$LockPath,
    [string]$ResultsDirectory
)

$ErrorActionPreference = 'Stop'
$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($AppDirectory)) {
    $AppDirectory = Join-Path $scriptRoot `
        '..\build\native-app\PPSA99005\dist\PPSA99005'
}
if ([string]::IsNullOrWhiteSpace($ResultsDirectory)) {
    $ResultsDirectory = Join-Path $scriptRoot '..\results\native-app'
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
$eboot = Join-Path $app 'eboot.bin'
$selectedTest = Join-Path (Split-Path (Split-Path $app -Parent) -Parent) `
    'selected-test.txt'
$cycle = [IO.Path]::GetFullPath((Join-Path $repo `
    '..\..\docs\ps5-homebrew-dev-protocol\scripts\Invoke-Ps5Cycle.ps1'))
$protocol = Split-Path (Split-Path $cycle -Parent) -Parent
$downloadData = Join-Path ([IO.Path]::GetTempPath()) `
    "ps5-opengl-download0-$PID-$([Guid]::NewGuid().ToString('N')).dat"
$receiptDirectory = Join-Path ([IO.Path]::GetTempPath()) `
    "ps5-opengl-receipt-$PID-$([Guid]::NewGuid().ToString('N'))"
$resultFile = $null
$postHealthChecked = $false
$lockReleased = $false
if ($FirstRegistration -and $Incremental) {
    throw 'The first registration cannot use an incremental upload.'
}

foreach ($required in @($eboot, (Join-Path $app 'sce_module\libc.prx'),
        (Join-Path $app 'sce_sys\param.json'), $selectedTest, $cycle)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required native-title artifact is missing: $required"
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

$actualGate = (Get-Content -LiteralPath $selectedTest -Raw).Trim()
if ($actualGate -ne $ExpectedGate) {
    throw "Selected gate mismatch: expected $ExpectedGate, built $actualGate"
}
$actualHash = (Get-FileHash -LiteralPath $eboot -Algorithm SHA256).Hash
if ($actualHash -ne $ExpectedEbootSha256) {
    throw "eboot.bin hash mismatch: expected $ExpectedEbootSha256, built $actualHash"
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
$dirtyConverter = @(& git -c "safe.directory=$boilerplate" -C $boilerplate `
    status --porcelain -- tooling/native/sce_module_writer.cpp)
if ($LASTEXITCODE -ne 0 -or $dirtyConverter.Count -ne 0) {
    throw 'The boilerplate native converter must be clean before a hardware cycle.'
}
$actualProtocolCommit = (& git -c "safe.directory=$protocol" -C $protocol rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualProtocolCommit -ne $ExpectedProtocolCommit) {
    throw 'Protocol commit mismatch.'
}
$dirtyProtocol = @(& git -c "safe.directory=$protocol" -C $protocol status --porcelain)
if ($LASTEXITCODE -ne 0 -or $dirtyProtocol.Count) {
    throw 'The hardware protocol repository must be clean before a cycle.'
}
$dirty = @(& git -c "safe.directory=$repo" -C $repo status --porcelain)
if ($LASTEXITCODE -ne 0 -or $dirty.Count -ne 0) {
    throw 'The OpenGL repository must be clean before a hardware cycle.'
}

$ps5Lock = [IO.Path]::GetFullPath($LockPath)
$lockToken = 'ps5-opengl-native-{0}-pid{1}-{2}' -f `
    $ExpectedGate.Replace('_', '-').Replace('.o', ''), $PID, `
    [Guid]::NewGuid().ToString('N').Substring(0, 8)
$handle = $null
for ($lockAttempt = 0; $lockAttempt -lt 5; ++$lockAttempt) {
    try {
        $handle = [IO.File]::Open(
            $ps5Lock,
            [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write,
            [IO.FileShare]::None)
        break
    } catch [IO.IOException] {
        if (Test-Path -LiteralPath $ps5Lock) {
            $owner = (Get-Content -LiteralPath $ps5Lock -Raw).Trim()
            throw "PS5 lock is occupied: $owner"
        }
        if ($lockAttempt -eq 4) {
            throw 'PS5 lock kept changing during acquisition.'
        }
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
        -LockPath $ps5Lock -LockToken $lockToken -ResultsDirectory $ResultsDirectory `
        -OwnerConfirmedIdle:$OwnerConfirmedIdle
    New-Item -ItemType Directory -Path $ResultsDirectory -Force | Out-Null
    $cycleArguments = @{
        TitleId = 'PPSA99005'
        AppDirectory = $app
        PreviousTitleId = 'PPSA99005'
        Ps5Host = $Ps5Host
        FtpPort = 2121
        KlogPort = 3232
        ElfPort = 9021
        ResultsDirectory = [IO.Path]::GetFullPath($ResultsDirectory)
        ObservationSeconds = $ObservationSeconds
        ObservationStopText = $ObservationStopText
        FtpCredential = $FtpCredential
        SkipVideoReadiness = $true
        SkipRoutineScreenshots = $true
        Headless = [bool]$Headless
    }
    if (-not $FirstRegistration) {
        $cycleArguments.UseExistingFolderRegistration = $true
    }
    if ($Incremental) {
        $cycleArguments.UploadRelativePaths = @(
            'eboot.bin', 'sce_module/libc.prx', 'sce_sys/param.json')
    }
    $runStarted = Get-Date
    & $cycle @cycleArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Managed PPSA99005 cycle failed with exit code $LASTEXITCODE."
    }
    $resultFile = Get-ChildItem -LiteralPath $ResultsDirectory `
        -Filter 'PPSA99005-*-result.json' -File |
        Where-Object LastWriteTime -GE $runStarted |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $resultFile) {
        throw 'Managed PPSA99005 cycle produced no result record.'
    }
    $downloadDataWsl = (wsl.exe wslpath -a -- $downloadData.Replace('\', '/')).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $downloadDataWsl) {
        throw 'Could not resolve receipt download path.'
    }
    & wsl.exe -e curl --fail --silent --show-error --disable-epsv `
        --connect-timeout 5 --max-time 120 `
        -u $FtpCredential `
        "ftp://${Ps5Host}:2121/user/download/PPSA99005/download0.dat" `
        --output $downloadDataWsl
    if ($LASTEXITCODE -ne 0 -or
        -not (Test-Path -LiteralPath $downloadData -PathType Leaf)) {
        throw 'Could not retrieve PPSA99005 download0 evidence.'
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
    New-Item -ItemType Directory -Path $receiptDirectory | Out-Null
    $dotnet = (Get-Command dotnet).Source
    $ufs2Runner = & (Join-Path $boilerplate 'tools\setup-ffpkg-tooling.ps1') `
        -Dotnet $dotnet
    if ($ufs2Runner -match '^/mnt/') {
        $downloadDataWsl = (wsl.exe wslpath -a -- `
            ($downloadData -replace '\\', '/')).Trim()
        $receiptDirectoryWsl = (wsl.exe wslpath -a -- `
            ($receiptDirectory -replace '\\', '/')).Trim()
        & wsl.exe sh $ufs2Runner extract $downloadDataWsl `
            $receiptDirectoryWsl /ps5-opengl.log
    } else {
        $previousRollForward = $env:DOTNET_ROLL_FORWARD
        try {
            $env:DOTNET_ROLL_FORWARD = 'Major'
            & $dotnet $ufs2Runner extract $downloadData `
                $receiptDirectory /ps5-opengl.log
        } finally {
            $env:DOTNET_ROLL_FORWARD = $previousRollForward
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not extract the PPSA99005 OpenGL receipt.'
    }
    $extracted = Join-Path $receiptDirectory 'ps5-opengl.log'
    if (-not (Test-Path -LiteralPath $extracted -PathType Leaf)) {
        throw 'PPSA99005 produced no ps5-opengl.log receipt.'
    }
    $receipt = $resultFile.FullName -replace '-result\.json$', '-opengl.log'
    Copy-Item -LiteralPath $extracted -Destination $receipt
    $receiptText = Get-Content -LiteralPath $receipt -Raw
    if ($receiptText -notmatch '\[ps5-opengl-native\] gate completed status=0') {
        throw "OpenGL gate did not report success; inspect $receipt"
    }
    $result = Get-Content -LiteralPath $resultFile.FullName -Raw |
        ConvertFrom-Json
    if ($result.outcome -ne 'entered-eboot' -or
        $result.teardownSignal -ne 'runtime-layers-released') {
        throw "OpenGL gate lifecycle did not complete cleanly: $($result.outcome)/$($result.teardownSignal)."
    }
    Write-Host "OPENGL_GATE_PASSED $ExpectedGate receipt=$receipt"
} finally {
    if (Test-Path -LiteralPath $downloadData) {
        Remove-Item -LiteralPath $downloadData -Force
    }
    if (Test-Path -LiteralPath $receiptDirectory) {
        $resolved = [IO.Path]::GetFullPath($receiptDirectory)
        if (-not $resolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()))) {
            throw 'Refusing cleanup outside the temporary directory.'
        }
        Remove-Item -LiteralPath $receiptDirectory -Recurse -Force
    }
    if ($resultFile) {
        @{ checkoutCommit = $actualCommit; protocolCommit = $actualProtocolCommit;
           gate = $ExpectedGate; ps5Host = $Ps5Host;
           ownerConfirmedIdle = [bool]$OwnerConfirmedIdle;
           postHealthChecked = $postHealthChecked; lockReleased = $lockReleased } |
            ConvertTo-Json | Set-Content -LiteralPath (
                $resultFile.FullName -replace '-result\.json$', '-runner.json')
    }
}
