$ErrorActionPreference = 'Stop'

$benchmarkRoot = 'E:\VanguardBench'
$payloadRoot = Join-Path $benchmarkRoot 'current'
$engineRoot = Join-Path $payloadRoot 'ENGINE'
$toolsRoot = Join-Path $payloadRoot 'TOOLS'
$captureRoot = Join-Path $benchmarkRoot 'captures'
$requestPath = Join-Path $benchmarkRoot 'request.json'
$latestResultPath = Join-Path $captureRoot 'latest_result.json'

$runtimeExe = Join-Path $engineRoot 'bin\Runtime\Profile\Vanguard.exe'
$projectFile = Join-Path $engineRoot 'games\Project Cardinal\project-cardinal.vproject'
$captureExe = Join-Path $toolsRoot 'tracy-capture.exe'
$csvExe = Join-Path $toolsRoot 'tracy-csvexport.exe'
$expectedD3D12Core = Join-Path $engineRoot 'bin\Runtime\Profile\D3D12\D3D12Core.dll'

New-Item -ItemType Directory -Force -Path $captureRoot | Out-Null

$request = [ordered]@{
    runId = 'desktop_profile'
    captureSeconds = 18
    warmupSeconds = 2
    windowWidth = 1280
    windowHeight = 720
    schedulerProbe = $false
    probeResources = 0
}

if (Test-Path -LiteralPath $requestPath)
{
    $loaded = Get-Content -LiteralPath $requestPath -Raw | ConvertFrom-Json
    if ($loaded.runId) { $request.runId = [string]$loaded.runId }
    if ($loaded.captureSeconds) { $request.captureSeconds = [int]$loaded.captureSeconds }
    if ($loaded.warmupSeconds -ne $null) { $request.warmupSeconds = [int]$loaded.warmupSeconds }
    if ($loaded.windowWidth) { $request.windowWidth = [int]$loaded.windowWidth }
    if ($loaded.windowHeight) { $request.windowHeight = [int]$loaded.windowHeight }
    if ($loaded.schedulerProbe -ne $null) { $request.schedulerProbe = [bool]$loaded.schedulerProbe }
    if ($loaded.probeResources -ne $null) { $request.probeResources = [int]$loaded.probeResources }
}

if ($request.captureSeconds -lt 3 -or $request.captureSeconds -gt 300)
{
    throw 'captureSeconds must be between 3 and 300 seconds.'
}
if ($request.warmupSeconds -lt 0 -or $request.warmupSeconds -gt 60)
{
    throw 'warmupSeconds must be between 0 and 60 seconds.'
}
if ($request.windowWidth -lt 1 -or $request.windowWidth -gt 16384 -or $request.windowHeight -lt 1 -or $request.windowHeight -gt 16384)
{
    throw 'windowWidth and windowHeight must be between 1 and 16384.'
}
if ($request.probeResources -ne 0 -and $request.probeResources -ne 64 -and $request.probeResources -ne 320)
{
    throw 'probeResources must be 0, 64, or 320.'
}
if ($request.probeResources -ne 0 -and -not $request.schedulerProbe)
{
    throw 'probeResources requires schedulerProbe=true.'
}

$safeRunId = ($request.runId -replace '[^A-Za-z0-9_.-]', '_')
if ([string]::IsNullOrWhiteSpace($safeRunId))
{
    $safeRunId = 'desktop_profile'
}

$tracePath = Join-Path $captureRoot ($safeRunId + '.tracy')
$frameCsvPath = Join-Path $captureRoot ($safeRunId + '_application_frame.csv')
$resolveCsvPath = Join-Path $captureRoot ($safeRunId + '_resolve.csv')
$runtimeOutPath = Join-Path $captureRoot ($safeRunId + '_runtime.out.txt')
$runtimeErrPath = Join-Path $captureRoot ($safeRunId + '_runtime.err.txt')
$runnerLogPath = Join-Path $captureRoot ($safeRunId + '_runner.log.txt')
$resultPath = Join-Path $captureRoot ($safeRunId + '_result.json')

$result = [ordered]@{
    runId = $safeRunId
    ok = $false
    validRendererThroughEnd = $false
    captureSeconds = [int]$request.captureSeconds
    warmupSeconds = [int]$request.warmupSeconds
    windowWidth = [int]$request.windowWidth
    windowHeight = [int]$request.windowHeight
    clientPixelWidth = 0
    clientPixelHeight = 0
    windowDpi = 0
    windowSeconds = 0.0
    n = 0
    fps = 0.0
    p50_ms = 0.0
    p95_ms = 0.0
    p99_ms = 0.0
    mean_ms = 0.0
    frameEnd_s = 0.0
    resolveEnd_s = 0.0
    rendererEndGap_ms = 0.0
    d3d12CorePath = $null
    runtimeSha256 = $null
    d3d12CoreSha256 = $null
    host = $null
    trace = $tracePath
    frameCsv = $frameCsvPath
    resolveCsv = $resolveCsvPath
    runtimeStdout = $runtimeOutPath
    runtimeStderr = $runtimeErrPath
    error = $null
}

$runtime = $null

function Write-RunnerLog([string] $message)
{
    $line = ('{0:O} {1}' -f (Get-Date), $message)
    Add-Content -LiteralPath $runnerLogPath -Value $line -Encoding utf8
}

function Wait-ForTracyListener([System.Diagnostics.Process] $process, [int] $timeoutSeconds)
{
    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    while ((Get-Date) -lt $deadline)
    {
        $process.Refresh()
        if ($process.HasExited)
        {
            throw ('Vanguard exited before Tracy became available. Exit code: ' + $process.ExitCode)
        }

        $listener = Get-NetTCPConnection -LocalPort 8086 -State Listen -ErrorAction SilentlyContinue |
            Where-Object { $_.OwningProcess -eq $process.Id } |
            Select-Object -First 1
        if ($listener -ne $null)
        {
            return
        }
        Start-Sleep -Milliseconds 150
    }
    throw 'Timed out waiting for Vanguard Tracy listener on 127.0.0.1:8086.'
}

function Get-Percentile([double[]] $values, [double] $fraction)
{
    if ($values.Count -eq 0) { return 0.0 }
    $index = [Math]::Floor(($values.Count - 1) * $fraction)
    return $values[$index]
}

function Get-ForegroundProcessId()
{
    if (-not ('VanguardBenchmarkNative' -as [type]))
    {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class VanguardBenchmarkNative
{
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("kernel32.dll")]
    public static extern uint GetCurrentThreadId();

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool attach);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool ShowWindowAsync(IntPtr window, int command);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool BringWindowToTop(IntPtr window);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr SetFocus(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr SetActiveWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern void SwitchToThisWindow(IntPtr window, bool altTab);

    [DllImport("user32.dll")]
    public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetClientRect(IntPtr window, out RECT rect);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetWindowRect(IntPtr window, out RECT rect);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);

    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr dpiContext);
}
'@
    }

    $window = [VanguardBenchmarkNative]::GetForegroundWindow()
    if ($window -eq [IntPtr]::Zero) { return 0 }
    [uint32]$processId = 0
    [void][VanguardBenchmarkNative]::GetWindowThreadProcessId($window, [ref]$processId)
    return [int]$processId
}

function Get-ClientPixelExtent([IntPtr] $window)
{
    if ($window -eq [IntPtr]::Zero)
    {
        throw 'Vanguard has no main window for pixel-extent validation.'
    }

    # PowerShell itself is DPI-unaware on this host, so query the foreign HWND from
    # a per-monitor-aware thread to avoid Windows virtualizing client coordinates.
    $perMonitorAwareV2 = [IntPtr](-4)
    $previousContext = [VanguardBenchmarkNative]::SetThreadDpiAwarenessContext($perMonitorAwareV2)
    if ($previousContext -eq [IntPtr]::Zero)
    {
        throw 'Could not enter per-monitor DPI-aware context for client pixel validation.'
    }
    try
    {
        $rect = New-Object VanguardBenchmarkNative+RECT
        if (-not [VanguardBenchmarkNative]::GetClientRect($window, [ref]$rect))
        {
            throw 'Could not query Vanguard client pixel extent.'
        }
        return [pscustomobject]@{
            width = [int]($rect.Right - $rect.Left)
            height = [int]($rect.Bottom - $rect.Top)
            dpi = [int][VanguardBenchmarkNative]::GetDpiForWindow($window)
        }
    }
    finally
    {
        [void][VanguardBenchmarkNative]::SetThreadDpiAwarenessContext($previousContext)
    }
}

function Request-ForegroundWindow([IntPtr] $window)
{
    if ($window -eq [IntPtr]::Zero) { return }
    $windowRect = New-Object VanguardBenchmarkNative+RECT
    if ([VanguardBenchmarkNative]::GetWindowRect($window, [ref]$windowRect))
    {
        $clickX = [int](($windowRect.Left + $windowRect.Right) / 2)
        $clickY = [int]($windowRect.Top + 10)
        if ([VanguardBenchmarkNative]::SetCursorPos($clickX, $clickY))
        {
            [VanguardBenchmarkNative]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
            [VanguardBenchmarkNative]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
        }
    }
    $foreground = [VanguardBenchmarkNative]::GetForegroundWindow()
    [uint32]$foregroundProcessId = 0
    [uint32]$foregroundThread = if ($foreground -ne [IntPtr]::Zero) {
        [VanguardBenchmarkNative]::GetWindowThreadProcessId($foreground, [ref]$foregroundProcessId)
    } else { 0 }
    [uint32]$targetProcessId = 0
    [uint32]$targetThread = [VanguardBenchmarkNative]::GetWindowThreadProcessId($window, [ref]$targetProcessId)
    [uint32]$currentThread = [VanguardBenchmarkNative]::GetCurrentThreadId()
    $attachedForeground = $false
    $attachedTarget = $false
    try
    {
        if ($foregroundThread -ne 0 -and $foregroundThread -ne $currentThread)
        {
            $attachedForeground = [VanguardBenchmarkNative]::AttachThreadInput($currentThread, $foregroundThread, $true)
        }
        if ($targetThread -ne 0 -and $targetThread -ne $currentThread)
        {
            $attachedTarget = [VanguardBenchmarkNative]::AttachThreadInput($currentThread, $targetThread, $true)
        }
        [void][VanguardBenchmarkNative]::ShowWindowAsync($window, 9)
        [VanguardBenchmarkNative]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
        [VanguardBenchmarkNative]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
        [VanguardBenchmarkNative]::SwitchToThisWindow($window, $true)
        [void][VanguardBenchmarkNative]::BringWindowToTop($window)
        [void][VanguardBenchmarkNative]::SetActiveWindow($window)
        [void][VanguardBenchmarkNative]::SetFocus($window)
        [void][VanguardBenchmarkNative]::SetForegroundWindow($window)
    }
    finally
    {
        if ($attachedTarget)
        {
            [void][VanguardBenchmarkNative]::AttachThreadInput($currentThread, $targetThread, $false)
        }
        if ($attachedForeground)
        {
            [void][VanguardBenchmarkNative]::AttachThreadInput($currentThread, $foregroundThread, $false)
        }
    }
}

try
{
    Set-Content -LiteralPath $runnerLogPath -Value '' -Encoding utf8
    Write-RunnerLog ('Run ' + $safeRunId + ' starting.')

    foreach ($required in @($runtimeExe, $projectFile, $captureExe, $csvExe, $expectedD3D12Core))
    {
        if (-not (Test-Path -LiteralPath $required))
        {
            throw ('Required benchmark file is missing: ' + $required)
        }
    }

    $cpuInfo = Get-CimInstance Win32_Processor | Select-Object -First 1
    $gpuInfo = Get-CimInstance Win32_VideoController | Where-Object { $_.Name -match 'NVIDIA' } | Select-Object -First 1
    $osInfo = Get-CimInstance Win32_OperatingSystem
    $powerScheme = ((& powercfg /GETACTIVESCHEME) -join ' ').Trim()
    $result.runtimeSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $runtimeExe).Hash
    $result.d3d12CoreSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $expectedD3D12Core).Hash
    $result.host = [ordered]@{
        computerName = $env:COMPUTERNAME
        userName = $env:USERNAME
        sessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
        osCaption = $osInfo.Caption
        osVersion = $osInfo.Version
        osBuild = $osInfo.BuildNumber
        cpuName = $cpuInfo.Name
        cpuMaxClockMHz = $cpuInfo.MaxClockSpeed
        gpuName = if ($gpuInfo) { $gpuInfo.Name } else { $null }
        gpuDriverVersion = if ($gpuInfo) { $gpuInfo.DriverVersion } else { $null }
        displayWidth = if ($gpuInfo) { $gpuInfo.CurrentHorizontalResolution } else { $null }
        displayHeight = if ($gpuInfo) { $gpuInfo.CurrentVerticalResolution } else { $null }
        displayRefreshHz = if ($gpuInfo) { $gpuInfo.CurrentRefreshRate } else { $null }
        powerScheme = $powerScheme
        foregroundVerified = $false
    }

    $sessionId = [System.Diagnostics.Process]::GetCurrentProcess().SessionId
    $logonUi = @(Get-Process -Name 'LogonUI' -ErrorAction SilentlyContinue | Where-Object { $_.SessionId -eq $sessionId })
    if ($logonUi.Count -ne 0)
    {
        throw 'Benchmark console session is locked. Unlock the physical console before capturing.'
    }

    $existing = @(Get-Process -Name 'Vanguard' -ErrorAction SilentlyContinue)
    if ($existing.Count -ne 0)
    {
        throw ('Refusing to start benchmark while Vanguard.exe is already running. Existing PID(s): ' + (($existing | Select-Object -ExpandProperty Id) -join ', '))
    }

    if ($request.schedulerProbe)
    {
        $env:VANGUARD_RENDER_GRAPH_SCHEDULER_PROBE = '1'
        if ($request.probeResources -eq 64)
        {
            $env:VANGUARD_RF_PROBE_RESOURCES = '64'
        }
        else
        {
            Remove-Item Env:VANGUARD_RF_PROBE_RESOURCES -ErrorAction SilentlyContinue
        }
    }
    else
    {
        Remove-Item Env:VANGUARD_RENDER_GRAPH_SCHEDULER_PROBE -ErrorAction SilentlyContinue
        Remove-Item Env:VANGUARD_RF_PROBE_RESOURCES -ErrorAction SilentlyContinue
    }

    $runtimeArguments = ('"' + $projectFile + '" --window-width ' + [string]$request.windowWidth + ' --window-height ' + [string]$request.windowHeight)
    $runtime = Start-Process `
        -FilePath $runtimeExe `
        -ArgumentList $runtimeArguments `
        -WorkingDirectory $engineRoot `
        -RedirectStandardOutput $runtimeOutPath `
        -RedirectStandardError $runtimeErrPath `
        -PassThru

    Write-RunnerLog ('Started Vanguard PID ' + $runtime.Id + ' at requested window extent ' + $request.windowWidth + 'x' + $request.windowHeight + '.')
    Wait-ForTracyListener $runtime 15
    Write-RunnerLog 'Tracy listener is ready.'

    Add-Type -AssemblyName Microsoft.VisualBasic
    $focusDeadline = (Get-Date).AddSeconds(5)
    $foregroundVerified = $false
    while ((Get-Date) -lt $focusDeadline)
    {
        try
        {
            $runtime.Refresh()
            Write-RunnerLog ('Foreground attempt: target HWND=' + $runtime.MainWindowHandle + ', current foreground PID=' + (Get-ForegroundProcessId) + '.')
            Request-ForegroundWindow $runtime.MainWindowHandle
            [Microsoft.VisualBasic.Interaction]::AppActivate($runtime.Id)
        }
        catch
        {
        }
        Start-Sleep -Milliseconds 150
        if ((Get-ForegroundProcessId) -eq $runtime.Id)
        {
            $foregroundVerified = $true
            break
        }
    }
    if (-not $foregroundVerified)
    {
        throw 'Could not make Vanguard the foreground application in the interactive benchmark session.'
    }
    $result.host.foregroundVerified = $true
    Write-RunnerLog 'Vanguard foreground state verified.'

    $runtime.Refresh()
    $clientExtent = Get-ClientPixelExtent $runtime.MainWindowHandle
    $result.clientPixelWidth = $clientExtent.width
    $result.clientPixelHeight = $clientExtent.height
    $result.windowDpi = $clientExtent.dpi
    if ($clientExtent.width -ne $request.windowWidth -or $clientExtent.height -ne $request.windowHeight)
    {
        throw ('Vanguard client pixel extent does not match the benchmark request. Requested ' + $request.windowWidth + 'x' +
               $request.windowHeight + ', got ' + $clientExtent.width + 'x' + $clientExtent.height + '.')
    }
    Write-RunnerLog ('Vanguard client pixel extent verified at ' + $clientExtent.width + 'x' + $clientExtent.height +
                     ' (DPI ' + $clientExtent.dpi + ').')

    $coreModule = @($runtime.Modules | Where-Object { $_.ModuleName -ieq 'D3D12Core.dll' }) | Select-Object -First 1
    if ($coreModule -eq $null)
    {
        throw 'Vanguard did not load D3D12Core.dll.'
    }

    $result.d3d12CorePath = [System.IO.Path]::GetFullPath($coreModule.FileName)
    $expectedCorePath = [System.IO.Path]::GetFullPath($expectedD3D12Core)
    if (-not [string]::Equals($result.d3d12CorePath, $expectedCorePath, [System.StringComparison]::OrdinalIgnoreCase))
    {
        throw ('Vanguard loaded the wrong D3D12Core.dll. Expected ' + $expectedCorePath + ', got ' + $result.d3d12CorePath)
    }

    if ($request.warmupSeconds -gt 0)
    {
        Write-RunnerLog ('Warming up for ' + $request.warmupSeconds + ' second(s).')
        Start-Sleep -Seconds $request.warmupSeconds
    }

    Write-RunnerLog ('Starting Tracy capture for ' + $request.captureSeconds + ' second(s).')
    & $captureExe -o $tracePath -f -s $request.captureSeconds | Add-Content -LiteralPath $runnerLogPath -Encoding utf8
    if ($LASTEXITCODE -ne 0)
    {
        throw ('tracy-capture failed with exit code ' + $LASTEXITCODE)
    }

    & $csvExe -u -f 'Application/Frame' $tracePath | Set-Content -LiteralPath $frameCsvPath -Encoding ascii
    if ($LASTEXITCODE -ne 0)
    {
        throw ('Application/Frame CSV export failed with exit code ' + $LASTEXITCODE)
    }

    & $csvExe -u -f 'RenderFlow/Resolve' $tracePath | Set-Content -LiteralPath $resolveCsvPath -Encoding ascii
    if ($LASTEXITCODE -ne 0)
    {
        throw ('RenderFlow/Resolve CSV export failed with exit code ' + $LASTEXITCODE)
    }

    $frames = @(Import-Csv -LiteralPath $frameCsvPath | Where-Object { $_.name -eq 'Application/Frame' })
    $resolves = @(Import-Csv -LiteralPath $resolveCsvPath | Where-Object { $_.name -eq 'RenderFlow/Resolve' })
    if ($frames.Count -eq 0)
    {
        throw 'Trace contains no exact Application/Frame zones.'
    }
    if ($resolves.Count -eq 0)
    {
        throw 'Trace contains no exact RenderFlow/Resolve zones.'
    }

    $frameEnd = [double](($frames | Measure-Object -Property ns_since_start -Maximum).Maximum)
    $resolveEnd = [double](($resolves | Measure-Object -Property ns_since_start -Maximum).Maximum)
    $endGapNs = $frameEnd - $resolveEnd

    $result.frameEnd_s = $frameEnd / 1e9
    $result.resolveEnd_s = $resolveEnd / 1e9
    $result.rendererEndGap_ms = $endGapNs / 1e6
    $result.validRendererThroughEnd = ([Math]::Abs($endGapNs) -lt 1e9)

    $windowSeconds = [Math]::Min(12.0, [Math]::Max(1.0, [double]$request.captureSeconds - 2.0))
    $cutoff = $frameEnd - ($windowSeconds * 1e9)
    [double[]]$values = @(
        $frames |
            Where-Object { [double]$_.ns_since_start -ge $cutoff } |
            ForEach-Object { [double]$_.exec_time_ns } |
            Sort-Object
    )
    if ($values.Count -eq 0)
    {
        throw 'No Application/Frame samples survived the final-window filter.'
    }

    $meanNs = ($values | Measure-Object -Average).Average
    $result.windowSeconds = $windowSeconds
    $result.n = $values.Count
    $result.fps = $values.Count / $windowSeconds
    $result.p50_ms = (Get-Percentile $values 0.50) / 1e6
    $result.p95_ms = (Get-Percentile $values 0.95) / 1e6
    $result.p99_ms = (Get-Percentile $values 0.99) / 1e6
    $result.mean_ms = $meanNs / 1e6

    if (-not $result.validRendererThroughEnd)
    {
        throw ('Renderer stopped before the end of the trace. Application/Frame to RenderFlow/Resolve gap: ' + $result.rendererEndGap_ms + ' ms')
    }

    $result.ok = $true
    Write-RunnerLog 'Benchmark completed successfully.'
}
catch
{
    $result.error = $_.Exception.Message
    Write-RunnerLog ('ERROR: ' + $result.error)
}
finally
{
    if ($runtime -ne $null)
    {
        try
        {
            $runtime.Refresh()
            if (-not $runtime.HasExited)
            {
                [void]$runtime.CloseMainWindow()
                $deadline = (Get-Date).AddSeconds(3)
                while ((Get-Date) -lt $deadline)
                {
                    $runtime.Refresh()
                    if ($runtime.HasExited) { break }
                    Start-Sleep -Milliseconds 100
                }
                $runtime.Refresh()
                if (-not $runtime.HasExited)
                {
                    Stop-Process -Id $runtime.Id -Force -ErrorAction SilentlyContinue
                    Write-RunnerLog ('Force-stopped benchmark-owned Vanguard PID ' + $runtime.Id + '.')
                }
            }
        }
        catch
        {
            Write-RunnerLog ('Cleanup warning: ' + $_.Exception.Message)
        }
    }

    $json = $result | ConvertTo-Json -Depth 4
    Set-Content -LiteralPath $resultPath -Value $json -Encoding utf8
    Set-Content -LiteralPath $latestResultPath -Value $json -Encoding utf8
}

if (-not $result.ok)
{
    exit 1
}
