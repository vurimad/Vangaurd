param(
    [string] $HostAlias = 'vanguard-bench',
    [string] $Label = 'normal_quad',
    [int] $CaptureSeconds = 18,
    [int] $WarmupSeconds = 2,
    [int] $WindowWidth = 1280,
    [int] $WindowHeight = 720,
    [switch] $SchedulerProbe,
    [ValidateSet(0, 64, 320)]
    [int] $ProbeResources = 0,
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

if ($WindowWidth -lt 1 -or $WindowWidth -gt 16384 -or $WindowHeight -lt 1 -or $WindowHeight -gt 16384)
{
    throw 'WindowWidth and WindowHeight must be between 1 and 16384.'
}

$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$remoteRoot = 'E:\VanguardBench'
$remotePayload = 'E:\VanguardBench\current'
$remoteRuntime = 'E:\VanguardBench\current\ENGINE\bin\Runtime\Profile'
$remoteProject = 'E:\VanguardBench\current\ENGINE\games\Project Cardinal'
$remoteTools = 'E:\VanguardBench\current\TOOLS'
$remoteCaptures = 'E:\VanguardBench\captures'
$taskName = 'VanguardBenchProfile'

$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$safeLabel = ($Label -replace '[^A-Za-z0-9_.-]', '_')
if ([string]::IsNullOrWhiteSpace($safeLabel)) { $safeLabel = 'normal_quad' }
$runId = $safeLabel + '_' + $timestamp

$localRuntime = Join-Path $repository 'bin\Runtime\Profile'
$localProject = Join-Path $repository 'games\Project Cardinal'
$captureExe = Join-Path $repository 'external\tracy\upstream\capture\build-vs2022\Release\tracy-capture.exe'
$csvExe = Join-Path $repository 'external\tracy\upstream\csvexport\build-vs2022\Release\tracy-csvexport.exe'
$runnerScript = Join-Path $PSScriptRoot 'desktop_benchmark_runner.ps1'
$registerScript = Join-Path $PSScriptRoot 'register_desktop_benchmark_task.ps1'
$resultRoot = Join-Path $repository 'build\profiling\desktop_runs'
$localRunRoot = Join-Path $resultRoot $runId

function Invoke-Ssh([string] $command)
{
    & ssh -o BatchMode=yes $HostAlias $command
    if ($LASTEXITCODE -ne 0)
    {
        throw ('SSH command failed with exit code ' + $LASTEXITCODE + ': ' + $command)
    }
}

function Send-File([string] $source, [string] $destination)
{
    if (-not (Test-Path -LiteralPath $source))
    {
        throw ('Local deployment file is missing: ' + $source)
    }
    & scp -q $source ($HostAlias + ':' + $destination)
    if ($LASTEXITCODE -ne 0)
    {
        throw ('SCP failed for ' + $source + ' -> ' + $destination)
    }
}

function Assert-NoRemoteVanguard()
{
    $existing = & ssh -o BatchMode=yes $HostAlias 'cmd /c tasklist | findstr /i Vanguard.exe'
    $status = $LASTEXITCODE
    if ($status -eq 0 -and -not [string]::IsNullOrWhiteSpace(($existing -join "`n")))
    {
        throw ('Vanguard.exe is already running on the benchmark host. Refusing to interfere with it.' + "`n" + ($existing -join "`n"))
    }
    if ($status -ne 0 -and $status -ne 1)
    {
        throw ('Failed to verify that Vanguard.exe is absent on the benchmark host. SSH exit code: ' + $status)
    }
}

if (-not $SkipBuild)
{
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($msbuild))
    {
        throw 'MSBuild could not be located through vswhere.'
    }
    & $msbuild (Join-Path $repository 'build\projects\vs2022\runtime.vcxproj') /m:1 /p:Configuration=Profile /p:Platform=x64 /nologo /v:minimal
    if ($LASTEXITCODE -ne 0)
    {
        throw ('Profile Runtime build failed with exit code ' + $LASTEXITCODE)
    }
}

foreach ($required in @(
    (Join-Path $localRuntime 'Vanguard.exe'),
    (Join-Path $localRuntime 'DATA000.vpak'),
    (Join-Path $localRuntime 'SDL3.dll'),
    (Join-Path $localRuntime 'nvToolsExt64_1.dll'),
    (Join-Path $localRuntime 'oo2ext_7_win64.dll'),
    (Join-Path $localRuntime 'D3D12\D3D12Core.dll'),
    (Join-Path $localProject 'project-cardinal.vproject'),
    $captureExe,
    $csvExe,
    $runnerScript,
    $registerScript))
{
    if (-not (Test-Path -LiteralPath $required))
    {
        throw ('Required local benchmark file is missing: ' + $required)
    }
}

$sessionOutput = & ssh -o BatchMode=yes $HostAlias 'qwinsta'
if ($LASTEXITCODE -ne 0 -or -not ($sessionOutput -match '(?im)^\s*>?console\s+Ark\s+\d+\s+Active\b'))
{
    throw 'Ark is not logged into an active interactive desktop session on the benchmark host.'
}

# Refuse before deployment so an unrelated live Runtime cannot have its payload touched.
Assert-NoRemoteVanguard

Invoke-Ssh 'cmd /c if not exist E:\VanguardBench mkdir E:\VanguardBench'
Send-File $runnerScript '/E:/VanguardBench/desktop_benchmark_runner.ps1'
Send-File $registerScript '/E:/VanguardBench/register_desktop_benchmark_task.ps1'

Invoke-Ssh 'powershell -NoProfile -ExecutionPolicy Bypass -File E:\VanguardBench\register_desktop_benchmark_task.ps1'

Send-File (Join-Path $localRuntime 'Vanguard.exe') '/E:/VanguardBench/current/ENGINE/bin/Runtime/Profile/Vanguard.exe'
Send-File (Join-Path $localRuntime 'DATA000.vpak') '/E:/VanguardBench/current/ENGINE/bin/Runtime/Profile/DATA000.vpak'
Send-File (Join-Path $localRuntime 'SDL3.dll') '/E:/VanguardBench/current/ENGINE/bin/Runtime/Profile/SDL3.dll'
Send-File (Join-Path $localRuntime 'nvToolsExt64_1.dll') '/E:/VanguardBench/current/ENGINE/bin/Runtime/Profile/nvToolsExt64_1.dll'
Send-File (Join-Path $localRuntime 'oo2ext_7_win64.dll') '/E:/VanguardBench/current/ENGINE/bin/Runtime/Profile/oo2ext_7_win64.dll'
Send-File (Join-Path $localRuntime 'D3D12\D3D12Core.dll') '/E:/VanguardBench/current/ENGINE/bin/Runtime/Profile/D3D12/D3D12Core.dll'
Send-File (Join-Path $localProject 'project-cardinal.vproject') '/E:/VanguardBench/current/ENGINE/games/Project Cardinal/project-cardinal.vproject'
Send-File $captureExe '/E:/VanguardBench/current/TOOLS/tracy-capture.exe'
Send-File $csvExe '/E:/VanguardBench/current/TOOLS/tracy-csvexport.exe'

$request = [ordered]@{
    runId = $runId
    captureSeconds = $CaptureSeconds
    warmupSeconds = $WarmupSeconds
    windowWidth = $WindowWidth
    windowHeight = $WindowHeight
    schedulerProbe = [bool]$SchedulerProbe
    probeResources = $ProbeResources
} | ConvertTo-Json
$localRequest = Join-Path (Join-Path $repository 'build\profiling') ('desktop_request_' + $runId + '.json')
Set-Content -LiteralPath $localRequest -Value $request -Encoding utf8
try
{
    Send-File $localRequest '/E:/VanguardBench/request.json'
}
finally
{
    Remove-Item -LiteralPath $localRequest -Force -ErrorAction SilentlyContinue
}

# Close the deployment-to-launch race as well as the pre-deployment guard above.
Assert-NoRemoteVanguard

Invoke-Ssh ('schtasks /Run /TN ' + $taskName)

$remoteResultWindows = $remoteCaptures + '\' + $runId + '_result.json'
$deadline = (Get-Date).AddSeconds([Math]::Max(120, $CaptureSeconds + $WarmupSeconds + 60))
$resultExists = $false
while ((Get-Date) -lt $deadline)
{
    Start-Sleep -Milliseconds 500
    & ssh -o BatchMode=yes $HostAlias ('cmd /c dir /b ' + $remoteResultWindows + ' ^>nul 2^>^&1') | Out-Null
    if ($LASTEXITCODE -eq 0)
    {
        $resultExists = $true
        break
    }
}

if (-not $resultExists)
{
    throw ('Timed out waiting for benchmark result: ' + $remoteResultWindows)
}

New-Item -ItemType Directory -Force -Path $localRunRoot | Out-Null

$remoteBaseScp = '/E:/VanguardBench/captures/'
$resultName = $runId + '_result.json'
& scp -q ($HostAlias + ':' + $remoteBaseScp + $resultName) (Join-Path $localRunRoot $resultName)
if ($LASTEXITCODE -ne 0)
{
    throw 'Failed to retrieve benchmark result JSON.'
}
$result = Get-Content -LiteralPath (Join-Path $localRunRoot $resultName) -Raw | ConvertFrom-Json

foreach ($remoteName in @(
    ($runId + '.tracy'),
    ($runId + '_application_frame.csv'),
    ($runId + '_resolve.csv'),
    ($runId + '_runner.log.txt'),
    ($runId + '_runtime.out.txt'),
    ($runId + '_runtime.err.txt')))
{
    & scp -q ($HostAlias + ':' + $remoteBaseScp + $remoteName) (Join-Path $localRunRoot $remoteName)
}

$result | Format-List runId, ok, validRendererThroughEnd, windowWidth, windowHeight, clientPixelWidth, clientPixelHeight, windowDpi, d3d12CorePath, runtimeSha256, d3d12CoreSha256, captureSeconds, windowSeconds, n, fps, p50_ms, p95_ms, p99_ms, mean_ms, rendererEndGap_ms, error
if ($result.host)
{
    $result.host | Format-List computerName, userName, sessionId, osCaption, osVersion, osBuild, cpuName, cpuMaxClockMHz, gpuName, gpuDriverVersion, displayWidth, displayHeight, displayRefreshHz, powerScheme, foregroundVerified
}
Write-Output ('Local artifacts: ' + $localRunRoot)

if (-not $result.ok)
{
    exit 1
}
