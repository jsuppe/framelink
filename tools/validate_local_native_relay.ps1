# Focused post-handoff check, not a full suite or performance campaign.
# Run only after the coordinator releases the exclusive Windows runtime lane.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ExpectedRevision,
    [Parameter(Mandatory=$true)][string]$EvidenceDirectory,
    [string]$BuildDirectory = 'build',
    [string]$GStreamerBin = 'C:\Program Files\gstreamer\1.0\msvc_x86_64\bin',
    [Parameter(Mandatory=$true)][switch]$RuntimeLaneReleased
)
$ErrorActionPreference = 'Stop'
if (-not $RuntimeLaneReleased) { throw 'Coordinator runtime-lane handoff is required.' }
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo
$actual = (& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot read source revision.' }
$expected = (& git rev-parse "$ExpectedRevision^{commit}").Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $expected) { throw 'Source revision does not match the requested check.' }
$changes = @(& git status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0 -or $changes.Count) { throw 'Use a clean exact-revision source checkout.' }
$active = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -like 'fl_*' -or $_.ProcessName -match '^(cmake|ctest|MSBuild)$'
})
if ($active.Count) { throw 'Another build/test executor is already active; do not compete.' }
$mutex = New-Object System.Threading.Mutex($false, 'Local\FrameLinkFocusedWindowsValidation')
$locked = $false
try {
    $locked = $mutex.WaitOne(0)
    if (-not $locked) { throw 'Another focused validator owns this Windows lane.' }
    if (Test-Path $EvidenceDirectory) { throw 'Evidence directory already exists; preserve previous results.' }
    $evidence = (New-Item -ItemType Directory -Path $EvidenceDirectory).FullName
    $state = @{ revision=$actual; startedUtc=[DateTime]::UtcNow.ToString('o'); status='running'; scope='focused local native relay'; tests=@() }
    $state | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $evidence 'status.json')
    $env:PATH = "$GStreamerBin;$env:PATH"
    try {
    # Capture the actual complete tracked-source manifest, not merely HEAD's name.
    $manifest = @(& git ls-files | ForEach-Object {
        @{ path=$_; sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $_).Hash.ToLowerInvariant() }
    })
    $manifest | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $evidence 'source-sha256.json')
        & cmake -S . -B $BuildDirectory -G 'Visual Studio 17 2022' -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GST=ON *> (Join-Path $evidence 'configure.log')
        if ($LASTEXITCODE -ne 0) { throw 'Configure failed; see configure.log.' }
        & cmake --build $BuildDirectory --config Release --target fl_gst_native_producer_win gstframelink fl_gst_native_epoch_c_api *> (Join-Path $evidence 'build.log')
        if ($LASTEXITCODE -ne 0) { throw 'Build failed; see build.log.' }
        $tests = @('gst_native_epoch_c_api', 'gst_relay_local_three_process_win', 'gst_relay_local_source_control_win', 'gst_relay_local_events_win', 'gst_relay_local_process_win', 'gst_relay_local_pool_win')
        $regex = '^(' + ($tests -join '|') + ')$'
        $inventory = & ctest --test-dir $BuildDirectory -C Release -N --show-only=json-v1 -R $regex
        if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate the focused tests.' }
        $inventory | Set-Content (Join-Path $evidence 'test-inventory.json')
        $selected = @((($inventory -join "`n") | ConvertFrom-Json).tests | ForEach-Object { $_.name })
        if ($selected.Count -ne $tests.Count -or @(Compare-Object $tests $selected).Count) { throw 'Focused selection is incomplete or unexpected.' }
        $state.tests = $selected
        & ctest --test-dir $BuildDirectory -C Release --output-on-failure -R $regex *> (Join-Path $evidence 'targeted.log')
        $result = $LASTEXITCODE
        Copy-Item (Join-Path $BuildDirectory 'Testing/Temporary/LastTest.log') (Join-Path $evidence 'LastTest.log')
        $state.testExit = $result
        if ($result -ne 0) { throw 'Focused tests failed; preserve this evidence before fixes.' }
        $state.status = 'passed'
    } catch {
        $state.status = 'failed'; $state.error = $_.Exception.Message
        throw
    } finally {
        $state.finishedUtc = [DateTime]::UtcNow.ToString('o')
        $state | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $evidence 'status.json')
    }
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
