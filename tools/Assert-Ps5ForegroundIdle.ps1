# Read-only preflight shared by the CTS and public-API native-title runners.
[CmdletBinding()]
param(
    [string]$ProtocolDirectory,
    [ValidatePattern('^[A-Za-z0-9.-]+$')][string]$Ps5Host,
    [string]$FtpCredential = 'anonymous:homebrew',
    [string]$LockPath,
    [string]$LockToken,
    [string]$ResultsDirectory,
    # Explicit owner confirmation for missing lifecycle history after a reboot.
    [switch]$OwnerConfirmedIdle,
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
if (-not $ProtocolDirectory) {
    $ProtocolDirectory = Join-Path $PSScriptRoot '..\..\..\docs\ps5-homebrew-dev-protocol'
}
. (Join-Path $ProtocolDirectory 'scripts\ShadowMountLifecycle.ps1')

function Assert-IdleTitleLog([string]$Text, [switch]$OwnerConfirmedIdle) {
    $titles = @([regex]::Matches($Text, '\[GAME\] started: (\S+) pid=') |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    if (-not $titles.Count) {
        if ($OwnerConfirmedIdle -and
            $Text -notmatch '\[GAME\] started:|\[MDBG\] (?:crash-candidate:|\S+ crashed )') {
            Write-Host 'PS5_FOREGROUND_IDLE source=owner-confirmation history=missing'
            return
        }
        throw 'Foreground state unknown: no title lifecycle in log.'
    }
    foreach ($title in $titles) {
        if ($title -notmatch '^PPSA\d{5}$') { throw "Unknown foreground title: $title" }
        $state = Get-ShadowMountTitleState -Text $Text -TitleId $title
        if ($state -notin @('runtime-layers-released', 'game-stopped')) {
            throw "Console is not idle: $title state=$state. No deployment or launch permitted."
        }
    }
}

if ($SelfTest) {
    $started = '[GAME] started: PPSA99004 pid=1'
    $released = '[LINK] runtime layers released: PPSA99004'
    Assert-IdleTitleLog "$started`n$released"
    Assert-IdleTitleLog "$started`n[KSTUFF] game stopped: PPSA99004 pid=1"
    Assert-IdleTitleLog "$started`n$released`n$started`n$released"
    Assert-IdleTitleLog "$started`n$released`n[GAME] started: PPSA99005 pid=2`n[LINK] runtime layers released: PPSA99005"
    Assert-IdleTitleLog '' -OwnerConfirmedIdle
    Assert-IdleTitleLog '[WATCHER] started' -OwnerConfirmedIdle
    foreach ($bad in @('', $started, "$released`n$started",
            "$started`n[MDBG] PPSA99004 crashed now",
            "$started`n$released`n$started",
            "$started`n$released`n[GAME] started: PPSA99005 pid=2",
            '[GAME] started: UNKNOWN pid=1', '[GAME] started: PPSA99004',
            '[MDBG] PPSA99004 crashed now', '[MDBG] crash-candidate: PPSA99004 pid=1')) {
        foreach ($confirmed in @($false, $true)) {
            if ($bad -eq '' -and $confirmed) { continue }
            $rejected = $false
            try { Assert-IdleTitleLog $bad -OwnerConfirmedIdle:$confirmed } catch { $rejected = $true }
            if (-not $rejected) { throw "Unsafe foreground log was accepted: $bad" }
        }
    }
    Write-Host 'foreground-idle: self-test PASS'
    return
}

if (-not $Ps5Host) { throw 'Specify -Ps5Host for a console preflight.' }

function Assert-OwnedLock {
    if (-not $LockToken -or -not (Test-Path -LiteralPath $LockPath) -or
        (Get-Content -LiteralPath $LockPath -Raw) -cne $LockToken) {
        throw 'Foreground preflight requires the exact owned lock.'
    }
}
Assert-OwnedLock
New-Item -ItemType Directory -Path $ResultsDirectory -Force | Out-Null
$snapshot = Join-Path ([IO.Path]::GetFullPath($ResultsDirectory)) (
    'foreground-{0}-{1}-{2}.log' -f $Ps5Host, (Get-Date -Format 'yyyyMMdd-HHmmssfff'), $PID)
$snapshotWsl = (wsl.exe wslpath -a -- $snapshot.Replace('\', '/')).Trim()
if ($LASTEXITCODE -ne 0 -or -not $snapshotWsl) { throw 'Could not resolve foreground log path.' }
& wsl.exe -e curl --fail --silent --show-error --disable-epsv `
    --connect-timeout 5 --max-time 30 -u $FtpCredential `
    "ftp://${Ps5Host}:2121/data/shadowmount/debug.log" --output $snapshotWsl
if ($LASTEXITCODE -ne 0) { throw "Could not read foreground state; inspect $snapshot" }
Assert-OwnedLock
Assert-IdleTitleLog (Get-Content -LiteralPath $snapshot -Raw) -OwnerConfirmedIdle:$OwnerConfirmedIdle
Write-Host "PS5_FOREGROUND_IDLE evidence=$snapshot"
