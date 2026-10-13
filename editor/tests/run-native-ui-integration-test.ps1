param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string]$Project,
    [Parameter(Mandatory=$true)][string]$Artifacts,
    [switch]$InteractivePersistenceLayoutTest,
    [switch]$InteractiveNativeUiTest,
    [switch]$InteractiveActivityTest
)
$ErrorActionPreference = 'Stop'
$selectedModes = [int]$InteractivePersistenceLayoutTest.IsPresent + [int]$InteractiveNativeUiTest.IsPresent +
                 [int]$InteractiveActivityTest.IsPresent
if ($selectedModes -gt 1) {
    throw 'Choose only one native UI integration-test validation mode per invocation.'
}
$projectWasSpecified = ![string]::IsNullOrWhiteSpace($Project)
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Artifacts = [IO.Path]::GetFullPath($Artifacts)
New-Item -ItemType Directory -Path $Artifacts -Force | Out-Null
if (!$Project) {
    $testProjectDirectory = Join-Path $Artifacts 'project'
    New-Item -ItemType Directory -Path $testProjectDirectory -Force | Out-Null
    $Project = Join-Path $testProjectDirectory 'native-ui-integration-test.vproject'
    if (!(Test-Path -LiteralPath $Project)) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'native-ui-integration-test.vproject') -Destination $Project }
}
$Project = (Resolve-Path -LiteralPath $Project).Path
$projectRoot = Split-Path -Parent $Project
foreach ($relative in @('Assets','DerivedData','Intermediate','Saved','Builds','Config','Plugins')) {
    New-Item -ItemType Directory -Path (Join-Path $projectRoot $relative) -Force | Out-Null
}

if ($InteractivePersistenceLayoutTest) {
    if ($projectWasSpecified) {
        throw 'Interactive persistence layout validation owns an isolated scratch project under -Artifacts; do not pass -Project.'
    }
    $ownedProjectRoot = [IO.Path]::GetFullPath((Join-Path $Artifacts 'project'))
    if (![string]::Equals([IO.Path]::GetFullPath($projectRoot), $ownedProjectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Interactive persistence layout validation refused a project root outside its owned artifact directory.'
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'native-ui-integration-test.vproject') -Destination $Project -Force
    $projectText = [IO.File]::ReadAllText($Project)
    $projectIdMatch = [regex]::Match($projectText, '(?m)^\s*project\.id\s*=\s*"([0-9a-fA-F-]{36})"\s*$')
    if (!$projectIdMatch.Success) { throw 'Interactive persistence layout validation could not resolve its scratch project ID.' }
    $sessionProjectsRoot = [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'CD Projekt Red\Red Engine\Vanguard\Sessions\Projects'))
    $editorStatePath = [IO.Path]::GetFullPath((Join-Path $sessionProjectsRoot $projectIdMatch.Groups[1].Value.ToLowerInvariant()))
    if (![string]::Equals([IO.Directory]::GetParent($editorStatePath).FullName, $sessionProjectsRoot.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Interactive persistence layout validation refused to clear a session path outside the per-user Projects root.'
    }
    if (Test-Path -LiteralPath $editorStatePath) {
        Remove-Item -LiteralPath $editorStatePath -Recurse -Force
    }
}

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class NativeUiTestCapture {
    delegate bool Visitor(IntPtr handle, IntPtr ignored);
    [StructLayout(LayoutKind.Sequential)] struct Rect { public int left, top, right, bottom; }
    [DllImport("user32.dll")] static extern bool EnumWindows(Visitor callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    public static int Save(uint process, string directory) {
        int count = 0;
        EnumWindows((window, ignored) => {
            uint owner; GetWindowThreadProcessId(window, out owner);
            Rect rect;
            if (owner != process || !IsWindowVisible(window) || !GetWindowRect(window, out rect)) return true;
            int width = rect.right - rect.left, height = rect.bottom - rect.top;
            if (width < 100 || height < 100) return true;
            using (var bitmap = new Bitmap(width, height)) {
                using (var graphics = Graphics.FromImage(bitmap)) {
                    var dc = graphics.GetHdc();
                    bool saved = PrintWindow(window, dc, 2);
                    graphics.ReleaseHdc(dc);
                    if (!saved) return true;
                }
                bitmap.Save(System.IO.Path.Combine(directory, "host-" + count + ".png"), ImageFormat.Png);
            }
            ++count;
            return true;
        }, IntPtr.Zero);
        return count;
    }
}
'@
$log = Join-Path $projectRoot 'Saved/Logs/VanguardEditor.log'

if ($InteractiveNativeUiTest -or $InteractiveActivityTest -or $InteractivePersistenceLayoutTest) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class NativeUiTestNative {
    delegate bool Visitor(IntPtr window, IntPtr ignored);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] struct MouseInput { public int dx, dy; public uint mouseData, flags, time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct KeyboardInput { public ushort virtualKey, scanCode; public uint flags, time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct HardwareInput { public uint message; public ushort low, high; }
    [StructLayout(LayoutKind.Explicit)] struct InputUnion {
        [FieldOffset(0)] public MouseInput mouse;
        [FieldOffset(0)] public KeyboardInput keyboard;
        [FieldOffset(0)] public HardwareInput hardware;
    }
    [StructLayout(LayoutKind.Sequential)] struct Input { public uint type; public InputUnion data; }
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] static extern bool EnumWindows(Visitor callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint count, Input[] inputs, int size);
    [DllImport("user32.dll")] public static extern short VkKeyScan(char value);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr window, StringBuilder text, int capacity);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    public static IntPtr[] Windows(uint process) {
        var result = new List<IntPtr>();
        EnumWindows((window, ignored) => { uint owner; GetWindowThreadProcessId(window, out owner); if (owner == process && IsWindowVisible(window)) result.Add(window); return true; }, IntPtr.Zero);
        return result.ToArray();
    }
    public static string Title(IntPtr window) { var text = new StringBuilder(256); GetWindowText(window, text, text.Capacity); return text.ToString(); }
    public static void Click(int x, int y) {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(120);
        var down = new Input { type = 0, data = new InputUnion { mouse = new MouseInput { flags = 0x0002 } } };
        var up = new Input { type = 0, data = new InputUnion { mouse = new MouseInput { flags = 0x0004 } } };
        int size = Marshal.SizeOf(typeof(Input));
        if (SendInput(1, new [] { down }, size) != 1) throw new InvalidOperationException("SendInput mouse-down failed");
        System.Threading.Thread.Sleep(60);
        if (SendInput(1, new [] { up }, size) != 1) throw new InvalidOperationException("SendInput mouse-up failed");
        System.Threading.Thread.Sleep(180);
    }
    public static void ClickAndMove(int x, int y, int moveX, int moveY) {
        SetCursorPos(x, y); System.Threading.Thread.Sleep(100); mouse_event(2,0,0,0,UIntPtr.Zero); mouse_event(4,0,0,0,UIntPtr.Zero);
        SetCursorPos(moveX, moveY); System.Threading.Thread.Sleep(180);
    }
    public static void Drag(int x1, int y1, int x2, int y2) {
        SetCursorPos(x1, y1); System.Threading.Thread.Sleep(150); mouse_event(2,0,0,0,UIntPtr.Zero); System.Threading.Thread.Sleep(250);
        for (int i=1; i<=50; ++i) { SetCursorPos(x1 + (x2-x1)*i/50, y1 + (y2-y1)*i/50); System.Threading.Thread.Sleep(30); }
        System.Threading.Thread.Sleep(1000); mouse_event(4,0,0,0,UIntPtr.Zero);
    }
    public static void Key(byte key) {
        var down = new Input { type = 1, data = new InputUnion { keyboard = new KeyboardInput { virtualKey = key, flags = 0 } } };
        var up = new Input { type = 1, data = new InputUnion { keyboard = new KeyboardInput { virtualKey = key, flags = 0x0002 } } };
        int size = Marshal.SizeOf(typeof(Input));
        if (SendInput(1, new [] { down }, size) != 1) throw new InvalidOperationException("SendInput key-down failed");
        System.Threading.Thread.Sleep(120);
        if (SendInput(1, new [] { up }, size) != 1) throw new InvalidOperationException("SendInput key-up failed");
        System.Threading.Thread.Sleep(120);
    }
    public static void TypeAscii(string text) {
        foreach (char c in text) {
            short mapped = VkKeyScan(c); if (mapped < 0) continue;
            byte key = (byte)(mapped & 0xff); byte modifiers = (byte)((mapped >> 8) & 0xff);
            if ((modifiers & 1) != 0) keybd_event(0x10,0,0,UIntPtr.Zero);
            keybd_event(key,0,0,UIntPtr.Zero); keybd_event(key,0,2,UIntPtr.Zero);
            if ((modifiers & 1) != 0) keybd_event(0x10,0,2,UIntPtr.Zero);
            System.Threading.Thread.Sleep(35);
        }
    }
}
'@
    if ([NativeUiTestNative]::SetThreadDpiAwarenessContext([IntPtr](-4)) -eq [IntPtr]::Zero) {
        throw 'Native UI integration-test driver could not enter Per-Monitor-V2 DPI awareness.'
    }
    function Get-LogCount { if (Test-Path -LiteralPath $log) { return @(Get-Content -LiteralPath $log).Count }; return 0 }
    function Wait-NewLogMatch([string]$pattern, [int]$after, [int]$seconds = 12) {
        $until = [DateTime]::UtcNow.AddSeconds($seconds)
        while ([DateTime]::UtcNow -lt $until) {
            if ($testProcess -and $testProcess.HasExited) {
                $tail = if (Test-Path -LiteralPath $log) { @(Get-Content -LiteralPath $log -Tail 40) -join "`n" } else { '<log missing>' }
                throw "Editor exited with code $($testProcess.ExitCode) while waiting for log pattern: $pattern`n$tail"
            }
            if (Test-Path -LiteralPath $log) {
                $lines = @(Get-Content -LiteralPath $log)
                if ($lines.Count -gt $after) {
                    $match = $lines[$after..($lines.Count-1)] | Where-Object { $_ -match $pattern } | Select-Object -Last 1
                    if ($match) { return $match }
                }
            }
            Start-Sleep -Milliseconds 100
        }
        throw "Timed out waiting for log pattern: $pattern"
    }
    function Latest-LogMatch([string]$pattern) {
        return Get-Content -LiteralPath $log | Where-Object { $_ -match $pattern } | Select-Object -Last 1
    }
    function Get-Coord([string]$line, [string]$name) {
        $match = [regex]::Match($line, [regex]::Escape($name) + '=([-0-9.]+),([-0-9.]+)')
        if (!$match.Success) { throw "Missing $name coordinate in: $line" }
        return @([double]$match.Groups[1].Value, [double]$match.Groups[2].Value)
    }
    function Get-Rect([string]$line, [string]$name) {
        $match = [regex]::Match($line, [regex]::Escape($name) + '=([-0-9.]+),([-0-9.]+),([-0-9.]+),([-0-9.]+)')
        if (!$match.Success) { throw "Missing $name rectangle in: $line" }
        return @([double]$match.Groups[1].Value, [double]$match.Groups[2].Value,
                 [double]$match.Groups[3].Value, [double]$match.Groups[4].Value)
    }
    function Get-UIntField([string]$line, [string]$name) {
        $match = [regex]::Match($line, [regex]::Escape($name) + '=([0-9]+)')
        if (!$match.Success) { throw "Missing $name field in: $line" }
        return [uint32]$match.Groups[1].Value
    }
    function Get-UInt64Field([string]$line, [string]$name) {
        $match = [regex]::Match($line, [regex]::Escape($name) + '=([0-9]+)')
        if (!$match.Success) { throw "Missing $name field in: $line" }
        return [uint64]$match.Groups[1].Value
    }
    function Get-FloatField([string]$line, [string]$name) {
        $match = [regex]::Match($line, [regex]::Escape($name) + '=([-0-9.]+)')
        if (!$match.Success) { throw "Missing $name field in: $line" }
        return [double]$match.Groups[1].Value
    }
    function Assert-ActivityProgress([string]$beforeLine, [string]$afterLine, [string]$label) {
        $beforeSamples = Get-UIntField $beforeLine 'samples'
        $afterSamples = Get-UIntField $afterLine 'samples'
        $beforeProducerTicks = Get-UIntField $beforeLine 'producerTicks'
        $afterProducerTicks = Get-UIntField $afterLine 'producerTicks'
        if ($afterSamples -le $beforeSamples -or $afterProducerTicks -le $beforeProducerTicks) {
            throw "Background Activity did not continue independently while $label`: samples $beforeSamples->$afterSamples producerTicks $beforeProducerTicks->$afterProducerTicks."
        }
    }
    function Assert-NoNewLogMatch([string]$pattern, [int]$after, [int]$milliseconds = 700) {
        Start-Sleep -Milliseconds $milliseconds
        if (!(Test-Path -LiteralPath $log)) { return }
        $lines = @(Get-Content -LiteralPath $log)
        if ($lines.Count -le $after) { return }
        $match = $lines[$after..($lines.Count-1)] | Where-Object { $_ -match $pattern } | Select-Object -First 1
        if ($match) { throw "Unexpected log pattern after disabled/unsupported interaction: $pattern`n$match" }
    }
    function Wait-ContextPopupAbsent([uint32]$sequence, [int]$after, [string]$expectedPattern) {
        $pattern = 'Editor native UI test context popup(?: absent)?: sequence=' + $sequence + ' '
        $outcome = Wait-NewLogMatch $pattern $after 8
        if ($outcome -notmatch 'context popup absent:' -or $outcome -notmatch $expectedPattern) {
            throw "Context popup sequence $sequence was expected to remain absent but was observed: $outcome"
        }
        return $outcome
    }
    function Wait-MenuDraw([int]$after, [double]$minimumWidth = 32.0, [int]$seconds = 8) {
        $until = [DateTime]::UtcNow.AddSeconds($seconds)
        while ([DateTime]::UtcNow -lt $until) {
            if ($testProcess -and $testProcess.HasExited) {
                throw "Editor exited with code $($testProcess.ExitCode) while waiting for real menu draw telemetry."
            }
            if (Test-Path -LiteralPath $log) {
                $lines = @(Get-Content -LiteralPath $log)
                if ($lines.Count -gt $after) {
                    $matches = @($lines[$after..($lines.Count-1)] | Where-Object { $_ -match 'Editor native UI test menu draw telemetry:' })
                    for ($index = $matches.Count - 1; $index -ge 0; --$index) {
                        $width = Get-FloatField $matches[$index] 'width'
                        if ($width -ge $minimumWidth) { return $matches[$index] }
                    }
                }
            }
            Start-Sleep -Milliseconds 100
        }
        throw "Timed out waiting for real menu draw width >= $minimumWidth."
    }
    function Find-Window([string]$exact, [string]$contains) {
        foreach ($window in [NativeUiTestNative]::Windows([uint32]$testProcess.Id)) {
            $title = [NativeUiTestNative]::Title($window)
            if (($exact -and $title -eq $exact) -or ($contains -and $title.Contains($contains))) { return $window }
        }
        return [IntPtr]::Zero
    }
    function Get-ReadyNativeWindowRect([IntPtr]$window, [string]$label, [int]$milliseconds = 2000) {
        $until = [DateTime]::UtcNow.AddMilliseconds($milliseconds)
        while ([DateTime]::UtcNow -lt $until) {
            $rect = New-Object NativeUiTestNative+Rect
            if ($window -ne [IntPtr]::Zero -and [NativeUiTestNative]::GetWindowRect($window, [ref]$rect) -and
                $rect.right -gt $rect.left -and $rect.bottom -gt $rect.top) {
                return $rect
            }
            Start-Sleep -Milliseconds 50
        }
        throw "Native UI integration test could not read native window rect for $label."
    }
    function Assert-PointInWindow([IntPtr]$window, [double]$x, [double]$y, [string]$label) {
        $rect = Get-ReadyNativeWindowRect $window $label
        if ($x -lt $rect.left -or $x -ge $rect.right -or $y -lt $rect.top -or $y -ge $rect.bottom) {
            throw "Native UI integration test $label coordinate $x,$y is outside native rect $($rect.left),$($rect.top),$($rect.right),$($rect.bottom)."
        }
    }
    function Get-NativeWindowRect([IntPtr]$window, [string]$label) {
        $rect = Get-ReadyNativeWindowRect $window $label
        return @([double]$rect.left, [double]$rect.top, [double]($rect.right - $rect.left), [double]($rect.bottom - $rect.top))
    }
    function Place-WindowInsideScreen([IntPtr]$window, $screen, [string]$label) {
        $rect = Get-ReadyNativeWindowRect $window $label
        $area = $screen.WorkingArea
        $marginX = [Math]::Min(120, [Math]::Max(24, [int]($area.Width / 12)))
        $marginY = [Math]::Min(100, [Math]::Max(24, [int]($area.Height / 12)))
        $maximumWidth = [Math]::Max(320, $area.Width - 2 * $marginX)
        $maximumHeight = [Math]::Max(240, $area.Height - 2 * $marginY)
        $currentWidth = $rect.right - $rect.left
        $currentHeight = $rect.bottom - $rect.top
        $width = [Math]::Min($maximumWidth, [Math]::Max([Math]::Min(700, $maximumWidth), $currentWidth))
        $height = [Math]::Min($maximumHeight, [Math]::Max([Math]::Min(500, $maximumHeight), $currentHeight))
        $x = $area.Left + [int](($area.Width - $width) / 2)
        $y = $area.Top + [int](($area.Height - $height) / 2)
        [NativeUiTestNative]::SetWindowPos($window, [IntPtr]::Zero, $x, $y, $width, $height, 0x0010) | Out-Null

        $until = [DateTime]::UtcNow.AddSeconds(2)
        do {
            $placed = Get-ReadyNativeWindowRect $window ($label + ' after monitor placement') 250
            if ($placed.left -ge $area.Left -and $placed.top -ge $area.Top -and
                $placed.right -le $area.Right -and $placed.bottom -le $area.Bottom) {
                return
            }
            Start-Sleep -Milliseconds 50
        } while ([DateTime]::UtcNow -lt $until)
        throw "Native UI integration test $label was not fully placed inside $($screen.DeviceName): window=$($placed.left),$($placed.top),$($placed.right),$($placed.bottom) workArea=$($area.Left),$($area.Top),$($area.Right),$($area.Bottom)."
    }
    function Wait-WindowGone([string]$exact, [int]$seconds = 12) {
        $until = [DateTime]::UtcNow.AddSeconds($seconds)
        while ([DateTime]::UtcNow -lt $until) {
            if ((Find-Window $exact '') -eq [IntPtr]::Zero) { return }
            Start-Sleep -Milliseconds 100
        }
        throw "Timed out waiting for native window to close: $exact"
    }
    function Wait-WindowReady([string]$exact, [string]$contains, [string]$label, [int]$seconds = 12) {
        $until = [DateTime]::UtcNow.AddSeconds($seconds)
        while ([DateTime]::UtcNow -lt $until) {
            if ($testProcess -and $testProcess.HasExited) {
                throw "Editor exited with code $($testProcess.ExitCode) while waiting for native window: $label"
            }
            $window = Find-Window $exact $contains
            if ($window -ne [IntPtr]::Zero -and ![NativeUiTestNative]::IsIconic($window)) {
                $rect = New-Object NativeUiTestNative+Rect
                if ([NativeUiTestNative]::GetWindowRect($window, [ref]$rect) -and
                    $rect.right -gt $rect.left -and $rect.bottom -gt $rect.top) {
                    return $window
                }
            }
            Start-Sleep -Milliseconds 100
        }
        throw "Timed out waiting for native window to become ready: $label"
    }
    function Raise-NativeWindow([IntPtr]$window) {
        [NativeUiTestNative]::ShowWindow($window, 9) | Out-Null
        # Briefly promote the intended editor host above unrelated desktop windows so the
        # following real mouse click cannot land on the terminal/browser when Windows
        # declines a cross-process SetForegroundWindow request. Immediately remove the
        # topmost bit again; the click remains the operation that establishes foreground.
        [NativeUiTestNative]::SetWindowPos($window, [IntPtr](-1), 0, 0, 0, 0, 0x0003) | Out-Null
        [NativeUiTestNative]::SetWindowPos($window, [IntPtr](-2), 0, 0, 0, 0, 0x0003) | Out-Null
        [NativeUiTestNative]::SetForegroundWindow($window) | Out-Null
        Start-Sleep -Milliseconds 75
    }
    function Focus-CommandPanel([IntPtr]$window, [int]$docked, [string]$label) {
        $pattern = 'Editor native UI test command telemetry: docked=' + $docked + ' focused=1'
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            Raise-NativeWindow $window
            # Disabled/unsupported menu items can intentionally leave shell chrome open.
            # Dismiss any retained popup before the real tab click so that click is not
            # consumed only as an outside-click close operation.
            [NativeUiTestNative]::Key(0x1B)
            Start-Sleep -Milliseconds 180
            $commandLine = Latest-LogMatch 'Editor native UI test command telemetry:'
            $commandDrag = Get-Coord $commandLine 'drag'
            Assert-PointInWindow $window $commandDrag[0] $commandDrag[1] $label
            $before = Get-LogCount
            [NativeUiTestNative]::Click([int][Math]::Round($commandDrag[0]), [int][Math]::Round($commandDrag[1]))
            try {
                [void](Wait-NewLogMatch $pattern $before 2)
                # Require the panel focus to survive beyond the activation/click frame.
                # After minimize/restore Windows can briefly report the intended tab as
                # focused and then settle native focus elsewhere; sending the shortcut in
                # that transient window makes a retained E3.3 routing check flaky.
                Start-Sleep -Milliseconds 400
                $stable = Latest-LogMatch 'Editor native UI test command telemetry:'
                if ($stable -match ('docked=' + $docked + ' focused=1(?: |$)')) { return }
                if ($attempt -eq 2) { throw "Native UI integration-test command panel focus was not stable for $label." }
            }
            catch {
                if ($attempt -eq 2) { throw }
                # A cross-host activation click can be consumed by Windows before SDL/ImGui
                # receives it. Retry only when Vanguard telemetry still says this panel is
                # unfocused, leaving enough time that the retry is a separate click.
                Start-Sleep -Milliseconds 650
            }
        }
    }
    function Focus-PanelText([IntPtr]$window, [int]$panel, [string]$label) {
        $telemetryPattern = 'Editor native UI test telemetry: panel=' + $panel
        $focusPattern = $telemetryPattern + '.*focused=1'
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            Raise-NativeWindow $window
            $panelLine = Latest-LogMatch $telemetryPattern
            $text = Get-Coord $panelLine 'text'
            Assert-PointInWindow $window $text[0] $text[1] $label
            $before = Get-LogCount
            [NativeUiTestNative]::Click([int][Math]::Round($text[0]), [int][Math]::Round($text[1]))
            try {
                [void](Wait-NewLogMatch $focusPattern $before 2)
                # Once the native host is active, click the actual input again so an
                # activation-only click cannot masquerade as text-widget focus.
                Start-Sleep -Milliseconds 650
                $panelLine = Latest-LogMatch $telemetryPattern
                $text = Get-Coord $panelLine 'text'
                Assert-PointInWindow $window $text[0] $text[1] $label
                [NativeUiTestNative]::Click([int][Math]::Round($text[0]), [int][Math]::Round($text[1]))
                Start-Sleep -Milliseconds 150
                return
            }
            catch {
                if ($attempt -eq 2) { throw }
                Start-Sleep -Milliseconds 650
            }
        }
    }
    function Select-BackgroundActivityTab([string]$label) {
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            $mainWindow = Wait-WindowReady '' 'Vanguard Editor' ($label + ' host attempt ' + ($attempt + 1)) 4
            $activityLine = Latest-LogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1'
            if ((Get-UIntField $activityLine 'visible') -eq 1) { return $activityLine }
            $activityDrag = Get-Coord $activityLine 'drag'
            Assert-PointInWindow $mainWindow $activityDrag[0] $activityDrag[1] ($label + ' attempt ' + ($attempt + 1))
            Raise-NativeWindow $mainWindow
            $before = Get-LogCount
            [NativeUiTestNative]::Click([int][Math]::Round($activityDrag[0]), [int][Math]::Round($activityDrag[1]))
            try {
                return Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1 visible=1' $before 4
            }
            catch {
                if ($attempt -eq 2) { throw }
                Start-Sleep -Milliseconds 500
            }
        }
        throw "Native UI integration test could not select Background Activity: $label"
    }
    function Select-ConsoleTabAndHideActivity([string]$label) {
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            $mainWindow = Wait-WindowReady '' 'Vanguard Editor' ($label + ' host attempt ' + ($attempt + 1)) 4
            $activityLine = Latest-LogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1'
            if ((Get-UIntField $activityLine 'visible') -eq 0) { return $activityLine }
            $consoleLine = Latest-LogMatch 'Editor native UI Console panel telemetry:.*docked=1'
            $consoleDrag = Get-Coord $consoleLine 'drag'
            Assert-PointInWindow $mainWindow $consoleDrag[0] $consoleDrag[1] ($label + ' attempt ' + ($attempt + 1))
            Raise-NativeWindow $mainWindow
            $before = Get-LogCount
            [NativeUiTestNative]::Click([int][Math]::Round($consoleDrag[0]), [int][Math]::Round($consoleDrag[1]))
            try {
                return Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1 visible=0' $before 4
            }
            catch {
                if ($attempt -eq 2) { throw }
                Start-Sleep -Milliseconds 500
            }
        }
        throw "Native UI integration test could not hide Background Activity behind Console: $label"
    }
    function Set-CommandPresentationMode([IntPtr]$window, [int]$docked, [int]$mode, [string]$label) {
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            Raise-NativeWindow $window
            # A disabled/unsupported menu item intentionally leaves its popup open. Close
            # shell chrome first so the next real click reaches the underlying test panel
            # instead of being consumed only as an outside-click dismissal.
            [NativeUiTestNative]::Key(0x1B)
            Start-Sleep -Milliseconds 180
            $line = Latest-LogMatch ('Editor native UI test command telemetry: docked=' + $docked)
            $control = Get-Coord $line 'modeControl'
            Assert-PointInWindow $window $control[0] $control[1] $label
            $before = Get-LogCount
            [NativeUiTestNative]::Click([int][Math]::Round($control[0]), [int][Math]::Round($control[1]))
            try {
                [void](Wait-NewLogMatch ('Editor native UI test command telemetry:.*mode=' + $mode) $before 3)
                return
            }
            catch {
                $latest = Latest-LogMatch ('Editor native UI test command telemetry: docked=' + $docked)
                if ($latest -match ('mode=' + $mode + '(?: |$)')) { return }
                if ($attempt -eq 2) { throw }
            }
        }
    }
    function Request-ContextFromOtherPanel([IntPtr]$sourceWindow, [int]$sourcePanel, [string]$sourceLabel,
                                           [IntPtr]$targetWindow, [int]$targetDocked,
                                           [int]$moveX, [int]$moveY, [string]$targetLabel) {
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            if ($sourceWindow -eq $targetWindow) {
                $refreshedWindow = Wait-WindowReady '' 'Vanguard Editor' ($sourceLabel + ' host attempt ' + ($attempt + 1)) 3
                $sourceWindow = $refreshedWindow
                $targetWindow = $refreshedWindow
            }
            # Restore another live panel as the committed Local receiver before every
            # attempt. This preserves the acceptance condition: the one request-control
            # click must both focus the command panel and request its popup in that same UI
            # authoring frame. The primary proof uses another panel in the same native host
            # so OS activation does not consume the semantic click.
            Focus-PanelText $sourceWindow $sourcePanel ($sourceLabel + ' attempt ' + ($attempt + 1))
            $commandLine = Latest-LogMatch ('Editor native UI test command telemetry: docked=' + $targetDocked)
            $requestControl = Get-Coord $commandLine 'request'
            Assert-PointInWindow $targetWindow $requestControl[0] $requestControl[1] $targetLabel
            Raise-NativeWindow $targetWindow
            # Synchronize the physical click to the test fixture's completed-frame
            # heartbeat. The request button must see the click while the mouse is still at
            # its request-time anchor; only after that UI frame commits do we move the real
            # cursor away, before the following frame authors the deferred popup.
            while ($heartbeat.WaitOne(0)) { }
            [NativeUiTestNative]::SetCursorPos([int][Math]::Round($requestControl[0]), [int][Math]::Round($requestControl[1])) | Out-Null
            if (!$heartbeat.WaitOne(2000)) { throw "Timed out positioning the cursor for $targetLabel." }
            while ($heartbeat.WaitOne(0)) { }
            $before = Get-LogCount
            [NativeUiTestNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
            [NativeUiTestNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
            if (!$heartbeat.WaitOne(2000)) { throw "Timed out waiting for the request frame for $targetLabel." }
            [NativeUiTestNative]::SetCursorPos($moveX, $moveY) | Out-Null
            try {
                $request = Wait-NewLogMatch 'Editor native UI test context request:.*mode=0 accepted=1 cancel=0' $before 3
                return [PSCustomObject]@{ Line = $request; Before = $before }
            }
            catch {
                if ($attempt -eq 2) { throw }
            }
        }
        throw "Native UI integration test could not activate $targetLabel from another panel."
    }
    function Request-ContextFromFocusedCommandPanel([IntPtr]$targetWindow, [int]$targetDocked,
                                                     [int]$moveX, [int]$moveY, [string]$targetLabel) {
        for ($attempt = 0; $attempt -lt 3; ++$attempt) {
            if ($targetDocked -eq 0) {
                $targetWindow = Wait-WindowReady 'Native UI command focus test' '' ($targetLabel + ' host attempt ' + ($attempt + 1)) 3
            }
            Focus-CommandPanel $targetWindow $targetDocked ($targetLabel + ' focus attempt ' + ($attempt + 1))
            $commandLine = Latest-LogMatch ('Editor native UI test command telemetry: docked=' + $targetDocked + ' focused=1')
            $requestControl = Get-Coord $commandLine 'request'
            Assert-PointInWindow $targetWindow $requestControl[0] $requestControl[1] $targetLabel
            while ($heartbeat.WaitOne(0)) { }
            [NativeUiTestNative]::SetCursorPos([int][Math]::Round($requestControl[0]), [int][Math]::Round($requestControl[1])) | Out-Null
            if (!$heartbeat.WaitOne(2000)) { throw "Timed out positioning the cursor for $targetLabel." }
            while ($heartbeat.WaitOne(0)) { }
            $before = Get-LogCount
            [NativeUiTestNative]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
            [NativeUiTestNative]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
            if (!$heartbeat.WaitOne(2000)) { throw "Timed out waiting for the request frame for $targetLabel." }
            [NativeUiTestNative]::SetCursorPos($moveX, $moveY) | Out-Null
            try {
                $request = Wait-NewLogMatch 'Editor native UI test context request:.*mode=0 accepted=1 cancel=0' $before 3
                return [PSCustomObject]@{ Line = $request; Before = $before }
            }
            catch {
                if ($attempt -eq 2) { throw }
            }
        }
        throw "Native UI integration test could not activate $targetLabel from the focused command panel."
    }
    function Get-MainMenuCommand([IntPtr]$mainWindow) {
        $surface = Latest-LogMatch 'Editor native UI test surface telemetry:'
        $root = Get-Coord $surface 'primaryMenu'
        Assert-PointInWindow $mainWindow $root[0] $root[1] 'primary File menu root'
        Raise-NativeWindow $mainWindow
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($root[0]), [int][Math]::Round($root[1]))
        $line = Wait-NewLogMatch 'Editor native UI test command telemetry:.*menu=(?!0\.0,0\.0)' $before 8
        $command = Get-Coord $line 'menu'
        Assert-PointInWindow $mainWindow $command[0] $command[1] 'primary menu command'
        return $command
    }
    function Capture-StallStack([string]$label) {
        $debugger = 'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe'
        if (!(Test-Path -LiteralPath $debugger)) { return $true }
        $stacks = Join-Path $Artifacts ($label + '.txt')
        $errors = Join-Path $Artifacts ($label + '.stderr.txt')
        $commands = '.echo NATIVE_UI_TEST_STALL_CAPTURE; .time; ~* kv; .detach; q'
        $arguments = '-p ' + $testProcess.Id + ' -y "' + (Split-Path -Parent $Executable) + '" -c "' + $commands + '"'
        $capture = Start-Process -FilePath $debugger -ArgumentList $arguments -WindowStyle Hidden -RedirectStandardOutput $stacks -RedirectStandardError $errors -PassThru
        if (!$capture.WaitForExit(15000)) { return $false }
        if ($null -ne $capture.ExitCode -and $capture.ExitCode -ne 0) { return $false }
        return [bool](Select-String -LiteralPath $stacks -Pattern '^Detached$' -Quiet)
    }

    if ($InteractivePersistenceLayoutTest) {
        function Assert-WindowInsideAnyWorkArea([IntPtr]$window, [string]$label) {
            $rect = Get-ReadyNativeWindowRect $window $label
            $screens = @([System.Windows.Forms.Screen]::AllScreens | Sort-Object `
                @{Expression={$_.WorkingArea.Left}}, @{Expression={$_.WorkingArea.Top}}, `
                @{Expression={$_.WorkingArea.Width}}, @{Expression={$_.WorkingArea.Height}}, `
                @{Expression={$_.DeviceName}})
            foreach ($screen in $screens) {
                $area = $screen.WorkingArea
                if ($rect.left -ge $area.Left -and $rect.top -ge $area.Top -and
                    $rect.right -le $area.Right -and $rect.bottom -le $area.Bottom) {
                    return
                }
            }
            throw "Interactive persistence $label is not fully contained by any current work area: $($rect.left),$($rect.top),$($rect.right),$($rect.bottom)."
        }

        function Assert-InteractivePersistenceLifecycle([string]$phase) {
            $lines = @(Get-Content -LiteralPath $log)
            $summary = $lines | Select-String -Pattern 'Editor native UI persistence lifecycle:' | Select-Object -Last 1
            if (!$summary) { throw "$phase did not emit native persistence lifecycle telemetry." }
            $match = [regex]::Match(
                $summary.Line,
                'shellActivate=(\d+) restore=(\d+) firstFrame=(\d+) afterFinal=(\d+) shellDeactivate=(\d+) canonical=(\d+)')
            if (!$match.Success) { throw "$phase emitted malformed persistence lifecycle telemetry: $($summary.Line)" }
            $activate = [UInt64]$match.Groups[1].Value
            $restore = [UInt64]$match.Groups[2].Value
            $firstFrame = [UInt64]$match.Groups[3].Value
            $afterFinal = [UInt64]$match.Groups[4].Value
            $deactivate = [UInt64]$match.Groups[5].Value
            if (!($activate -lt $restore -and $restore -lt $firstFrame -and $firstFrame -lt $afterFinal -and $afterFinal -lt $deactivate) -or
                [int]$match.Groups[6].Value -ne 1) {
                throw "$phase did not prove Activate < Restore < first frame < final save < Shell deactivate with a valid canonical file."
            }

            $ids = @{
                Ui = [Convert]::ToUInt64('6564756973657201', 16)
                Shell = [Convert]::ToUInt64('65647368656c6c01', 16)
                Test = [Convert]::ToUInt64('6564756974657301', 16)
                UserState = [Convert]::ToUInt64('6564757374737601', 16)
            }
            function Find-Sequence([UInt64]$service, [int]$stage) {
                $pattern = 'Editor persistence lifecycle event: sequence=(\d+) service=' + $service + ' stage=' + $stage + ' '
                $entry = $lines | Select-String -Pattern $pattern | Select-Object -Last 1
                if (!$entry) { throw "$phase is missing lifecycle event service=$service stage=$stage." }
                return [UInt64]$entry.Matches[0].Groups[1].Value
            }
            $uiStart = Find-Sequence $ids.Ui 3
            $shellStart = Find-Sequence $ids.Shell 3
            $testStart = Find-Sequence $ids.Test 3
            $userStart = Find-Sequence $ids.UserState 3
            if (!($uiStart -lt $shellStart -and $shellStart -lt $testStart -and $testStart -lt $userStart)) {
                throw "$phase startup ordering was not UI < Shell < fixture < UserState."
            }
            $userQuiesce = Find-Sequence $ids.UserState 4
            $testQuiesce = Find-Sequence $ids.Test 4
            $shellQuiesce = Find-Sequence $ids.Shell 4
            $uiQuiesce = Find-Sequence $ids.Ui 4
            if (!($userQuiesce -lt $testQuiesce -and $testQuiesce -lt $shellQuiesce -and $shellQuiesce -lt $uiQuiesce)) {
                throw "$phase quiesce ordering was not UserState < fixture < Shell < UI."
            }
        }

        function Start-InteractivePersistencePhase([string]$phase, [string]$scenario) {
            if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
            $script:testProcess = Start-Process -FilePath $Executable `
                -ArgumentList ('"' + $Project + '"'), '--inspect-editor-ui', $scenario `
                -WorkingDirectory $Artifacts -PassThru
            [void](Wait-NewLogMatch 'Editor native UI test command telemetry:' 0 45)
            Write-Output "Interactive persistence phase ready: $phase"
        }

        function Complete-InteractivePersistencePhase([string]$phase) {
            $main = Wait-WindowReady '' 'Vanguard Editor' ($phase + ' primary host')
            [NativeUiTestNative]::PostMessage($main, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            if (!$testProcess.WaitForExit(15000)) {
                throw "$phase did not exit after primary WM_CLOSE."
            }
            $testProcess.WaitForExit()
            if ($testProcess.ExitCode -ne 0) { throw "$phase exited with code $($testProcess.ExitCode)." }
            Assert-InteractivePersistenceLifecycle $phase
        }

        $env:VG_EDITOR_NATIVE_UI_TEST_AUTOMATION = '1'
        try {
            Start-InteractivePersistencePhase 'layout-seed' '--persistence-seed'
            $dockedLine = Wait-NewLogMatch 'Editor native UI test command telemetry: docked=1' 0 15
            $main = Wait-WindowReady '' 'Vanguard Editor' 'layout-seed primary host'
            $seedPrimaryDpi = [NativeUiTestNative]::GetDpiForWindow($main)
            if ($seedPrimaryDpi -eq 0) { throw 'layout-seed primary host reported zero DPI.' }
            $screens = @([System.Windows.Forms.Screen]::AllScreens | Sort-Object `
                @{Expression={$_.WorkingArea.Left}}, @{Expression={$_.WorkingArea.Top}}, `
                @{Expression={$_.WorkingArea.Width}}, @{Expression={$_.WorkingArea.Height}}, `
                @{Expression={$_.DeviceName}})
            $targetScreen = $screens | Where-Object { !$_.Primary } | Select-Object -First 1
            if (!$targetScreen) {
                $targetScreen = $screens | Where-Object { $_.Primary } | Select-Object -First 1
                $area = $targetScreen.WorkingArea
                [NativeUiTestNative]::SetWindowPos(
                    $main, [IntPtr]::Zero, $area.Left + 32, $area.Top + 32,
                    [Math]::Max(640, [int]($area.Width * 0.65)),
                    [Math]::Max(480, [int]($area.Height * 0.75)), 0x0010) | Out-Null
            }
            # A single-monitor seed moves/resizes the primary host above. Refresh the fixture's
            # desktop-space tab coordinate after that native placement change; using the earlier
            # telemetry would drag at the tab's stale pre-move location.
            Focus-CommandPanel $main 1 'layout-seed docked command panel'
            $dockedLine = Latest-LogMatch 'Editor native UI test command telemetry: docked=1'
            if (!$dockedLine) { throw 'layout-seed did not retain docked command telemetry after host placement.' }
            $area = $targetScreen.WorkingArea
            $targetX = $area.Right - [Math]::Min(140, [Math]::Max(60, [int]($area.Width / 12)))
            $targetY = $area.Top + [Math]::Min(240, [Math]::Max(80, [int]($area.Height / 5)))
            $drag = Get-Coord $dockedLine 'drag'
            Assert-PointInWindow $main $drag[0] $drag[1] 'layout-seed command panel drag target'
            $before = Get-LogCount
            [NativeUiTestNative]::Drag([int][Math]::Round($drag[0]), [int][Math]::Round($drag[1]), $targetX, $targetY)
            [void](Wait-NewLogMatch 'Editor native UI test command telemetry: docked=0' $before 15)
            $detached = Wait-WindowReady 'Native UI command focus test' '' 'layout-seed detached command host'
            Place-WindowInsideScreen $detached $targetScreen 'layout-seed detached command host'
            Assert-WindowInsideAnyWorkArea $detached 'layout-seed detached command host'
            $seedDetachedDpi = [NativeUiTestNative]::GetDpiForWindow($detached)
            if ($seedDetachedDpi -eq 0) { throw 'layout-seed detached command host reported zero DPI.' }
            Complete-InteractivePersistencePhase 'layout-seed'
            if ($seedPrimaryDpi -ne $seedDetachedDpi) {
                $seedSummary = Latest-LogMatch 'Editor native UI test command summary:'
                if (!$seedSummary -or $seedSummary -notmatch 'dpiTransition=1') {
                    throw "layout-seed crossed DPI domains ($seedPrimaryDpi->$seedDetachedDpi) without recording the command-panel DPI transition: $seedSummary"
                }
            }

            Start-InteractivePersistencePhase 'layout-restore' '--persistence-restore'
            [void](Wait-NewLogMatch 'Editor native UI test command telemetry: docked=0' 0 15)
            $detached = Wait-WindowReady 'Native UI command focus test' '' 'layout-restore detached command host'
            Assert-WindowInsideAnyWorkArea $detached 'layout-restore detached command host'
            $restoredDetachedDpi = [NativeUiTestNative]::GetDpiForWindow($detached)
            if ($restoredDetachedDpi -eq 0) {
                throw 'layout-restore detached command host reported zero DPI.'
            }
            if ($restoredDetachedDpi -ne $seedDetachedDpi) {
                throw "layout-restore detached host DPI changed on unchanged topology: seed=$seedDetachedDpi restore=$restoredDetachedDpi."
            }
            Focus-CommandPanel $detached 0 'layout-restore detached command panel'
            $beforeShortcut = Get-LogCount
            [NativeUiTestNative]::Key(0x7B)
            [void](Wait-NewLogMatch 'Editor native UI test command: invokes=1 attempts=1 rejected=0' $beforeShortcut 8)
            Complete-InteractivePersistencePhase 'layout-restore'

            Start-InteractivePersistencePhase 'layout-topology' '--persistence-topology'
            [void](Wait-NewLogMatch 'Editor native UI test command telemetry: docked=0' 0 15)
            $detached = Wait-WindowReady 'Native UI command focus test' '' 'layout-topology detached command host'
            Assert-WindowInsideAnyWorkArea $detached 'layout-topology detached command host'
            if ([NativeUiTestNative]::GetDpiForWindow($detached) -eq 0) {
                throw 'layout-topology detached command host reported zero DPI.'
            }
            Complete-InteractivePersistencePhase 'layout-topology'

            Write-Output 'Interactive editor persistence layout/restart/topology sequence PASS.'
            exit 0
        }
        finally {
            if ($testProcess -and !$testProcess.HasExited) {
                Stop-Process -Id $testProcess.Id -Force -ErrorAction SilentlyContinue
            }
            Remove-Item Env:VG_EDITOR_NATIVE_UI_TEST_AUTOMATION -ErrorAction SilentlyContinue
        }
    }

    $env:VG_EDITOR_NATIVE_UI_TEST_AUTOMATION = '1'
    $env:VG_EDITOR_NATIVE_UI_TEST_STALL_CAPTURE = '1'
    $testProcess = Start-Process -FilePath $Executable -ArgumentList ('"' + $Project + '"'), '--inspect-editor-ui' -WorkingDirectory $Artifacts -PassThru
    $heartbeat = $null
    $preserveProcess = $false
    try {
        $startupDeadline = [DateTime]::UtcNow.AddSeconds(45)
        while (!$testProcess.HasExited -and !$heartbeat -and [DateTime]::UtcNow -lt $startupDeadline) {
            try { $heartbeat = [Threading.EventWaitHandle]::OpenExisting("Local\VanguardNativeUiTestHeartbeat-$($testProcess.Id)") }
            catch [Threading.WaitHandleCannotBeOpenedException] { Start-Sleep -Milliseconds 100 }
        }
        if ($testProcess.HasExited) { throw "Native UI integration-test editor exited during startup with code $($testProcess.ExitCode)." }
        if (!$heartbeat) {
            if (!(Capture-StallStack 'startup-stall')) { $preserveProcess = $true; throw 'Native UI integration-test startup stack capture did not detach; editor process preserved for diagnosis.' }
            throw 'Native UI integration-test editor heartbeat object did not appear during startup; inspect startup-stall.txt when available.'
        }
        if (!$heartbeat.WaitOne(90000)) {
            if (!(Capture-StallStack 'first-frame-stall')) { $preserveProcess = $true; throw 'Native UI integration-test first-frame stack capture did not detach; editor process preserved for diagnosis.' }
            throw 'Native UI integration-test editor did not complete its first UI proof frame within 90 seconds after heartbeat creation; inspect first-frame-stall.txt when available.'
        }
        if (!(Test-Path -LiteralPath $log)) { throw 'Native UI integration-test editor log did not appear after the first completed proof frame.' }
        [void](Wait-NewLogMatch 'Editor native UI test command telemetry:.*toolbarMax=1 toolbarOffPrimary=0' 0 15)
        [void](Wait-NewLogMatch 'Editor native UI test telemetry: panel=1' 0 15)
        [void](Wait-NewLogMatch 'Editor native UI Activity diagnostic copy: worker=1' 0 15)
        [void](Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1.*published=1.*publishedState=2' 0 15)
        [void](Wait-NewLogMatch 'Editor native UI Console panel telemetry:.*docked=1' 0 15)
        $surfaceLine = Wait-NewLogMatch 'Editor native UI test surface telemetry:' 0 15
        $mainViewport = Get-UIntField $surfaceLine 'mainViewport'
        $menuViewport = Get-UIntField $surfaceLine 'menuViewport'
        $toolbarViewport = Get-UIntField $surfaceLine 'toolbarViewport'
        if ($mainViewport -eq 0 -or $menuViewport -ne $mainViewport -or $toolbarViewport -ne $mainViewport) {
            throw "Native UI integration-test persistent surfaces were not authored on the primary ImGui viewport: $surfaceLine"
        }
        Start-Sleep -Milliseconds 500

        $main = Wait-WindowReady '' 'Vanguard Editor' 'primary editor host'
        $detached = Wait-WindowReady 'Detached UI integration test' '' 'detached test host'
        $mainDpi = [NativeUiTestNative]::GetDpiForWindow($main)

        if ($InteractiveActivityTest) {
            # Focused E3.5 real-input acceptance. This is the P5-prepared minimum path:
            # the production Background Activity panel must progress while hidden and detached,
            # survive redocking, route exactly one physical-mouse cancellation through the
            # public Activity service, then publish Cancelling and stable Cancelled states.
            $screens = [System.Windows.Forms.Screen]::AllScreens | Sort-Object { $_.Bounds.Left }
            $secondary = $screens | Where-Object { !$_.Primary } | Select-Object -First 1
            if (!$secondary) { $secondary = $screens | Select-Object -Last 1 }
            $targetX = $secondary.Bounds.Left + [Math]::Min(500, [Math]::Max(100, [int]($secondary.Bounds.Width / 3)))
            $targetY = $secondary.Bounds.Top + [Math]::Min(300, [Math]::Max(100, [int]($secondary.Bounds.Height / 3)))

            $activityLine = Select-BackgroundActivityTab 'initial Background Activity selection'
            $activityHiddenLine = Select-ConsoleTabAndHideActivity 'Console selection for hidden Activity proof'
            $afterHidden = Get-LogCount
            $activityHiddenProgressLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1 visible=0' $afterHidden 15
            Assert-ActivityProgress $activityHiddenLine $activityHiddenProgressLine 'the product panel remained hidden behind another dock tab'

            $activitySelectedLine = Select-BackgroundActivityTab 'hidden Background Activity reselect'
            Assert-ActivityProgress $activityHiddenProgressLine $activitySelectedLine 'the hidden product tab was reselected'

            $activityDrag = Get-Coord $activitySelectedLine 'drag'
            $before = Get-LogCount
            [NativeUiTestNative]::Drag([int][Math]::Round($activityDrag[0]), [int][Math]::Round($activityDrag[1]), $targetX, $targetY)
            $activityDetachedLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=0 visible=1' $before 15
            $activityWindow = Wait-WindowReady 'Background Activity' 'Background Activity' 'detached Background Activity host'
            Place-WindowInsideScreen $activityWindow $secondary 'detached Background Activity host'
            $afterDetach = Get-LogCount
            $activityDetachedProgressLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=0 visible=1' $afterDetach 15
            Assert-ActivityProgress $activityDetachedLine $activityDetachedProgressLine 'the product panel remained detached'

            $activityRedockedLine = $null
            for ($redockAttempt = 0; $redockAttempt -lt 3; ++$redockAttempt) {
                $activityWindow = Wait-WindowReady 'Background Activity' 'Background Activity' ('detached Background Activity redock attempt ' + ($redockAttempt + 1)) 4
                $latestDetachedLine = Latest-LogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=0 visible=1'
                $activityDrag = Get-Coord $latestDetachedLine 'drag'
                $main = Wait-WindowReady '' 'Vanguard Editor' ('primary editor redock target attempt ' + ($redockAttempt + 1)) 4
                $panel0Window = Get-Rect (Latest-LogMatch 'Editor native UI test telemetry: panel=0') 'window'
                $activityRedockX = [int][Math]::Round($panel0Window[0] + $panel0Window[2] * 0.5)
                $activityRedockY = [int][Math]::Round($panel0Window[1] + $panel0Window[3] * 0.5)
                Assert-PointInWindow $main $activityRedockX $activityRedockY ('Background Activity redock target attempt ' + ($redockAttempt + 1))
                Raise-NativeWindow $activityWindow
                $before = Get-LogCount
                [NativeUiTestNative]::Drag([int][Math]::Round($activityDrag[0]), [int][Math]::Round($activityDrag[1]),
                                      $activityRedockX, $activityRedockY)
                try {
                    $activityRedockedLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1' $before 5
                    break
                }
                catch {
                    if ($redockAttempt -eq 2) { throw }
                    Start-Sleep -Milliseconds 500
                }
            }
            Wait-WindowGone 'Background Activity'
            Assert-ActivityProgress $activityDetachedProgressLine $activityRedockedLine 'the product panel was redocked'

            $main = Wait-WindowReady '' 'Vanguard Editor' 'primary editor host before Activity cancellation'
            Focus-CommandPanel $main 1 'Activity cancellation control'
            $activityCancelControl = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=1 focused=1') 'activityCancel'
            Assert-PointInWindow $main $activityCancelControl[0] $activityCancelControl[1] 'Activity cancellation control'
            $before = Get-LogCount
            [NativeUiTestNative]::Click([int][Math]::Round($activityCancelControl[0]), [int][Math]::Round($activityCancelControl[1]))
            [void](Wait-NewLogMatch 'Editor native UI Activity cancel interaction:.*interactions=1 queued=1' $before 8)
            [void](Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*cancels=1 accepted=1 uiInteractions=1 queued=1 published=1.*publishedState=2 cancelQueued=1 cancelResult=1' $before 8)
            [void](Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*cancels=1 accepted=1 uiInteractions=1 queued=1 published=1.*publishedState=3' $before 8)
            $activityTerminalLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*cancels=1 accepted=1 uiInteractions=1 queued=1 published=1.*publishedState=6' $before 8
            $afterTerminal = Get-LogCount
            $activityStableTerminalLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*cancels=1 accepted=1 uiInteractions=1 queued=1 published=1.*publishedState=6' $afterTerminal 8
            if ((Get-UIntField $activityTerminalLine 'cancels') -ne 1 -or
                (Get-UIntField $activityStableTerminalLine 'cancels') -ne 1 -or
                (Get-UIntField $activityStableTerminalLine 'accepted') -ne 1) {
                throw 'Background Activity cancellation owner callback was retried or duplicated.'
            }

            # The command tab used for the physical cancel click hides Background Activity.
            # Reselect the real product tab and require the stable terminal publication there.
            $terminalVisibleLine = Select-BackgroundActivityTab 'terminal Background Activity reopen'
            if ($terminalVisibleLine -notmatch 'cancels=1 accepted=1 uiInteractions=1 queued=1 published=1' -or
                $terminalVisibleLine -notmatch 'publishedState=6') {
                $before = Get-LogCount
                [void](Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1 visible=1.*cancels=1 accepted=1 uiInteractions=1 queued=1 published=1.*publishedState=6' $before 15)
            }

            $main = Wait-WindowReady '' 'Vanguard Editor' 'primary editor host before focused Activity shutdown'
            [NativeUiTestNative]::PostMessage($main, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            if (!$testProcess.WaitForExit(15000)) { throw 'Focused Activity native test editor did not exit after primary WM_CLOSE.' }
            $testProcess.WaitForExit()

            $activitySummary = Latest-LogMatch 'Editor native UI Activity summary:'
            if (!$activitySummary -or $activitySummary -notmatch 'uiInteractions=1 queued=1 callbacks=1 accepted=1 admission=0 state=6') {
                throw "Focused Activity native cancellation summary failed: $activitySummary"
            }
            if ((Get-UInt64Field $activitySummary 'sourceGeneration') -eq 0 -or
                (Get-UInt64Field $activitySummary 'operation') -eq 0 -or
                (Get-UIntField $activitySummary 'samples') -eq 0 -or
                (Get-UIntField $activitySummary 'producerTicks') -eq 0) {
                throw "Focused Activity native source did not publish/progress: $activitySummary"
            }
            Write-Output $activitySummary
            Write-Output "Focused Activity real-input test PASS; screenshots=$Artifacts"
            exit 0
        }

        Focus-CommandPanel $main 1 'initial command tab'
        $before = Get-LogCount
        [NativeUiTestNative]::Key(0x77)
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=1' $before)

        $panel0 = Latest-LogMatch 'Editor native UI test telemetry: panel=0'
        $panel0Click = Get-Coord $panel0 'click'
        Assert-PointInWindow $main $panel0Click[0] $panel0Click[1] 'panel0 click'
        Focus-PanelText $main 0 'panel0 text'
        [NativeUiTestNative]::TypeAscii('alpha')
        Start-Sleep -Milliseconds 200
        [NativeUiTestNative]::Click([int][Math]::Round($panel0Click[0]), [int][Math]::Round($panel0Click[1]))
        Start-Sleep -Milliseconds 200

        $panel1 = Latest-LogMatch 'Editor native UI test telemetry: panel=1'
        $panel1Click = Get-Coord $panel1 'click'
        Assert-PointInWindow $detached $panel1Click[0] $panel1Click[1] 'panel1 click'
        Focus-PanelText $detached 1 'panel1 text'
        [NativeUiTestNative]::TypeAscii('beta')
        Start-Sleep -Milliseconds 200
        [NativeUiTestNative]::Click([int][Math]::Round($panel1Click[0]), [int][Math]::Round($panel1Click[1]))
        Start-Sleep -Milliseconds 200

        # Prove the newly-focused-panel context transaction while the command panel and
        # panel 0 are still visible in separate primary-host dock regions. Focusing panel 0
        # first makes the request-control click the operation that establishes the command
        # panel's Local receiver and requests the popup in the same authoring frame.
        $mainRect = Get-NativeWindowRect $main 'primary context host'
        $laterCursorX = [int][Math]::Round($mainRect[0] + $mainRect[2] - 48)
        $laterCursorY = [int][Math]::Round($mainRect[1] + $mainRect[3] - 48)
        $primaryRequestResult = Request-ContextFromOtherPanel $main 0 'primary panel before newly-focused context request' `
            $main 1 $laterCursorX $laterCursorY 'primary context request control'
        $primaryRequest = $primaryRequestResult.Line
        $before = $primaryRequestResult.Before
        $primarySequence = Get-UIntField $primaryRequest 'sequence'
        $primaryAnchor = Get-Coord $primaryRequest 'anchor'
        $primaryPopup = Wait-NewLogMatch ('Editor native UI test context popup: sequence=' + $primarySequence + ' ') $before 8
        $primaryRequestFrame = Get-UIntField $primaryRequest 'frame'
        $primaryPopupFrame = Get-UIntField $primaryPopup 'uiFrame'
        if ($primaryPopupFrame -ne ($primaryRequestFrame + 1)) { throw "Primary context popup did not open on the exact next UI frame: request=$primaryRequestFrame popup=$primaryPopupFrame." }
        $primaryPopupRect = Get-Rect $primaryPopup 'rect'
        $primaryPopupItem = Get-Coord $primaryPopup 'item'
        $primaryPopupCursor = Get-Coord $primaryPopup 'cursor'
        Assert-PointInWindow $main $primaryPopupItem[0] $primaryPopupItem[1] 'primary context popup item'
        $anchorCursorDistance = [Math]::Sqrt([Math]::Pow($primaryAnchor[0] - $primaryPopupCursor[0], 2) + [Math]::Pow($primaryAnchor[1] - $primaryPopupCursor[1], 2))
        $popupAnchorDistance = [Math]::Sqrt([Math]::Pow($primaryPopupRect[0] - $primaryAnchor[0], 2) + [Math]::Pow($primaryPopupRect[1] - $primaryAnchor[1], 2))
        $popupCursorDistance = [Math]::Sqrt([Math]::Pow($primaryPopupRect[0] - $primaryPopupCursor[0], 2) + [Math]::Pow($primaryPopupRect[1] - $primaryPopupCursor[1], 2))
        if ($anchorCursorDistance -lt 80 -or $popupAnchorDistance -ge $popupCursorDistance) {
            throw "Primary context popup did not prove request-time anchoring: anchor=$($primaryAnchor -join ',') laterCursor=$($primaryPopupCursor -join ',') popup=$($primaryPopupRect[0]),$($primaryPopupRect[1])."
        }
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($primaryPopupItem[0]), [int][Math]::Round($primaryPopupItem[1]))
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=2' $before)

        # Close the requesting panel after a fresh request commits but before its deferred
        # popup frame; test support reopens the same panel afterward.
        Focus-CommandPanel $main 1 'primary context cancellation tab'
        $cancelControl = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=1') 'cancel'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($cancelControl[0]), [int][Math]::Round($cancelControl[1]))
        $cancelRequest = Wait-NewLogMatch 'Editor native UI test context request:.*accepted=1 cancel=1' $before 8
        $cancelSequence = Get-UIntField $cancelRequest 'sequence'
        [void](Wait-NewLogMatch ('Editor native UI test context cancel: sequence=' + $cancelSequence + ' action=close') $before 8)
        [void](Wait-NewLogMatch ('Editor native UI test context cancel: sequence=' + $cancelSequence + ' action=reopen') $before 8)
        [void](Wait-ContextPopupAbsent $cancelSequence $before 'cancel=1')

        $screens = [System.Windows.Forms.Screen]::AllScreens | Sort-Object { $_.Bounds.Left }
        $secondary = $screens | Where-Object { !$_.Primary } | Select-Object -First 1
        if (!$secondary) { $secondary = $screens | Select-Object -Last 1 }
        $targetX = $secondary.Bounds.Left + [Math]::Min(500, [Math]::Max(100, [int]($secondary.Bounds.Width / 3)))
        $targetY = $secondary.Bounds.Top + [Math]::Min(300, [Math]::Max(100, [int]($secondary.Bounds.Height / 3)))

        Focus-CommandPanel $main 1 'command tab before detach'
        $commandDrag = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=1 focused=1') 'drag'
        $before = Get-LogCount
        [NativeUiTestNative]::Drag([int][Math]::Round($commandDrag[0]), [int][Math]::Round($commandDrag[1]), $targetX, $targetY)
        $detachedCommandLine = Wait-NewLogMatch 'Editor native UI test command telemetry: docked=0' $before 15
        $commandWindow = Find-Window 'Native UI command focus test' ''
        if ($commandWindow -eq [IntPtr]::Zero) { throw 'Native UI integration-test command panel did not create a detached native host.' }
        Place-WindowInsideScreen $commandWindow $secondary 'detached command host'
        $commandDpi = [NativeUiTestNative]::GetDpiForWindow($commandWindow)
        [NativeUiTestCapture]::Save([uint32]$testProcess.Id, (New-Item -ItemType Directory -Path (Join-Path $Artifacts 'command-detached') -Force).FullName) | Out-Null
        Focus-CommandPanel $commandWindow 0 'detached command tab'
        $before = Get-LogCount
        [NativeUiTestNative]::Key(0x77)
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=3' $before)

        $commandDrag = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=0') 'drag'
        $panel0Window = Get-Rect (Latest-LogMatch 'Editor native UI test telemetry: panel=0') 'window'
        $redockX = [int][Math]::Round($panel0Window[0] + $panel0Window[2] * 0.5)
        $redockY = [int][Math]::Round($panel0Window[1] + $panel0Window[3] * 0.5)
        Raise-NativeWindow $commandWindow
        $before = Get-LogCount
        [NativeUiTestNative]::Drag([int][Math]::Round($commandDrag[0]), [int][Math]::Round($commandDrag[1]),
                              $redockX, $redockY)
        [void](Wait-NewLogMatch 'Editor native UI test command telemetry: docked=1' $before 15)
        Wait-WindowGone 'Native UI command focus test'
        Focus-CommandPanel $main 1 'redocked command tab'
        $before = Get-LogCount
        [NativeUiTestNative]::Key(0x77)
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=4' $before)

        $detached = Find-Window 'Detached UI integration test' ''
        $beforeDpi = [NativeUiTestNative]::GetDpiForWindow($detached)
        $before = Get-LogCount
        [NativeUiTestNative]::SetWindowPos($detached, [IntPtr]::Zero, $secondary.Bounds.Left + 240, $secondary.Bounds.Top + 180, 700, 500, 0x0010) | Out-Null
        [void](Wait-NewLogMatch 'Editor native UI test telemetry: panel=1.*window=[-0-9.]+,[-0-9.]+,700\.0,500\.0' $before 15)
        $afterDpi = [NativeUiTestNative]::GetDpiForWindow($detached)

        $hosts = [NativeUiTestNative]::Windows([uint32]$testProcess.Id)
        if ($hosts.Length -lt 2) { throw 'Native UI integration test minimize check requires at least two native hosts.' }
        $before = Get-LogCount
        foreach ($window in $hosts) { [NativeUiTestNative]::ShowWindow($window, 6) | Out-Null }
        Start-Sleep -Milliseconds 700
        foreach ($window in $hosts) { if (![NativeUiTestNative]::IsIconic($window)) { throw 'Native UI integration-test host failed to enter minimized state.' } }
        [void](Wait-NewLogMatch 'Editor native UI test atlas minimized accepted:.*allMinimized=1' $before 15)
        foreach ($window in $hosts) { [NativeUiTestNative]::ShowWindow($window, 9) | Out-Null }
        Start-Sleep -Seconds 2
        foreach ($window in $hosts) { [NativeUiTestNative]::ShowWindow($window, 9) | Out-Null }
        [NativeUiTestCapture]::Save([uint32]$testProcess.Id, (New-Item -ItemType Directory -Path (Join-Path $Artifacts 'after-restore') -Force).FullName) | Out-Null

        $before = Get-LogCount
        [void](Wait-NewLogMatch 'Editor native UI test telemetry: panel=1' $before 15)
        $panel1 = Latest-LogMatch 'Editor native UI test telemetry: panel=1'
        $panel1Click = Get-Coord $panel1 'click'
        $detached = Wait-WindowReady 'Detached UI integration test' '' 'panel1 after restore'
        Focus-PanelText $detached 1 'panel1 text after restore'
        [NativeUiTestNative]::TypeAscii('r')
        [NativeUiTestNative]::Click([int][Math]::Round($panel1Click[0]), [int][Math]::Round($panel1Click[1]))

        $main = Wait-WindowReady '' 'Vanguard Editor' 'primary editor host after restore'
        Focus-CommandPanel $main 1 'post-restore command tab'
        $before = Get-LogCount
        [NativeUiTestNative]::Key(0x77)
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=5' $before)

        # E3.4 production-surface acceptance stays additive to the established native
        # docking/input/DPI/minimized-atlas flow above. The command panel remains the
        # Local receiver while shell chrome temporarily owns toolkit focus.
        $commandLine = Latest-LogMatch 'Editor native UI test command telemetry: docked=1'
        $toolbar = Get-Coord $commandLine 'toolbar'
        Assert-PointInWindow $main $toolbar[0] $toolbar[1] 'primary toolbar command'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($toolbar[0]), [int][Math]::Round($toolbar[1]))
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=6' $before)

        $beforeDefaultMenu = Get-LogCount
        $menuCommand = Get-MainMenuCommand $main
        $defaultMenuDraw = Wait-MenuDraw $beforeDefaultMenu
        $defaultMenuWidth = Get-FloatField $defaultMenuDraw 'width'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($menuCommand[0]), [int][Math]::Round($menuCommand[1]))
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=7' $before)

        # Change the effective binding only through the public command service and leave
        # the real File menu open long enough to capture the changed shortcut hint.
        Focus-CommandPanel $main 1 'command tab before shortcut override'
        $commandLine = Latest-LogMatch 'Editor native UI test command telemetry: docked=1'
        $shortcutControl = Get-Coord $commandLine 'shortcutControl'
        Assert-PointInWindow $main $shortcutControl[0] $shortcutControl[1] 'shortcut override control'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($shortcutControl[0]), [int][Math]::Round($shortcutControl[1]))
        [void](Wait-NewLogMatch 'Editor native UI test shortcut: custom=1 .*bound=1' $before 8)
        $beforeUncheckedMenu = Get-LogCount
        [void](Get-MainMenuCommand $main)
        $uncheckedDraw = Wait-MenuDraw $beforeUncheckedMenu ($defaultMenuWidth + 2.0)
        $uncheckedMenuIndices = Get-UIntField $uncheckedDraw 'indices'
        $customMenuWidth = Get-FloatField $uncheckedDraw 'width'
        if ($customMenuWidth -le $defaultMenuWidth + 2.0) { throw "Native UI integration test shortcut hint did not widen the real menu: default=$defaultMenuWidth custom=$customMenuWidth." }
        $shortcutCaptures = [NativeUiTestCapture]::Save([uint32]$testProcess.Id, (New-Item -ItemType Directory -Path (Join-Path $Artifacts 'shortcut-f12') -Force).FullName)
        if ($shortcutCaptures -lt 1) { throw 'F12-shortcut visual evidence capture produced no native host images.' }
        [NativeUiTestNative]::Key(0x1B)
        Focus-CommandPanel $main 1 'command tab after shortcut override'
        $before = Get-LogCount
        [NativeUiTestNative]::Key(0x7B)
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=8' $before)

        # Disabled remains structurally present but cannot dispatch from either persistent surface.
        Set-CommandPresentationMode $main 1 1 'disabled mode control'
        $toolbar = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=1.*mode=1') 'toolbar'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($toolbar[0]), [int][Math]::Round($toolbar[1]))
        Assert-NoNewLogMatch 'Editor native UI test command(?: rejected)?:' $before
        $menuCommand = Get-MainMenuCommand $main
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($menuCommand[0]), [int][Math]::Round($menuCommand[1]))
        Assert-NoNewLogMatch 'Editor native UI test command(?: rejected)?:' $before
        [NativeUiTestNative]::Key(0x1B)

        # Unsupported is still present disabled on persistent surfaces, but the contextual
        # contribution is omitted; its surrounding separators therefore normalize to empty.
        Set-CommandPresentationMode $main 1 2 'unsupported mode control'
        $toolbar = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=1.*mode=2') 'toolbar'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($toolbar[0]), [int][Math]::Round($toolbar[1]))
        Assert-NoNewLogMatch 'Editor native UI test command(?: rejected)?:' $before
        $menuCommand = Get-MainMenuCommand $main
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($menuCommand[0]), [int][Math]::Round($menuCommand[1]))
        Assert-NoNewLogMatch 'Editor native UI test command(?: rejected)?:' $before
        [NativeUiTestNative]::Key(0x1B)
        Focus-CommandPanel $main 1 'unsupported context command tab'
        $requestControl = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=1.*mode=2') 'request'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($requestControl[0]), [int][Math]::Round($requestControl[1]))
        $unsupportedRequest = Wait-NewLogMatch 'Editor native UI test context request:.*mode=2 accepted=1 cancel=0' $before 8
        $unsupportedSequence = Get-UIntField $unsupportedRequest 'sequence'
        [void](Wait-ContextPopupAbsent $unsupportedSequence $before 'mode=2 cancel=0')

        # Checked state is captured from the real menu presentation path and must add
        # draw geometry relative to the otherwise-identical unchecked F12 presentation.
        Set-CommandPresentationMode $main 1 3 'checked mode control'
        $beforeCheckedMenu = Get-LogCount
        [void](Get-MainMenuCommand $main)
        $checkedDraw = Wait-NewLogMatch 'Editor native UI test menu draw telemetry:' $beforeCheckedMenu 8
        $checkedIndices = Get-UIntField $checkedDraw 'indices'
        if ($checkedIndices -le $uncheckedMenuIndices) { throw "Checked command did not add real menu draw geometry: unchecked=$uncheckedMenuIndices checked=$checkedIndices." }
        $checkedCaptures = [NativeUiTestCapture]::Save([uint32]$testProcess.Id, (New-Item -ItemType Directory -Path (Join-Path $Artifacts 'checked-command') -Force).FullName)
        if ($checkedCaptures -lt 1) { throw 'Checked-command visual evidence capture produced no native host images.' }
        [NativeUiTestNative]::Key(0x1B)

        # The rejected Invoke is a real enabled surface activation. A later shell frame and
        # a subsequent successful surface activation prove the failure is item-local.
        Set-CommandPresentationMode $main 1 4 'rejected invoke mode control'
        $toolbar = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=1.*mode=4') 'toolbar'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($toolbar[0]), [int][Math]::Round($toolbar[1]))
        $rejected = Wait-NewLogMatch 'Editor native UI test command rejected:.*rejected=1' $before 8
        $afterRejected = Get-LogCount
        [void](Wait-NewLogMatch 'Editor native UI test surface telemetry:' $afterRejected 8)
        Set-CommandPresentationMode $main 1 0 'enabled recovery mode control'
        $toolbar = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=1.*mode=0') 'toolbar'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($toolbar[0]), [int][Math]::Round($toolbar[1]))
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=9' $before)

        # E3.5 Background Activity stays a real product panel. Select it by its visible
        # tab, hide it by selecting the real Console sibling tab, then detach/redock that
        # same product tab. Progress is proved from the synthetic source/service counters,
        # never from panel draw calls or elapsed time alone.
        $main = Wait-WindowReady '' 'Vanguard Editor' 'primary editor host before Activity panel proof'
        $activityLine = Latest-LogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1'
        if ((Get-UIntField $activityLine 'visible') -ne 1) {
            $activityDrag = Get-Coord $activityLine 'drag'
            Assert-PointInWindow $main $activityDrag[0] $activityDrag[1] 'Background Activity tab'
            $before = Get-LogCount
            [NativeUiTestNative]::Click([int][Math]::Round($activityDrag[0]), [int][Math]::Round($activityDrag[1]))
            $activityLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1 visible=1' $before 15
        }
        $activityVisibleLine = $activityLine
        $consoleLine = Latest-LogMatch 'Editor native UI Console panel telemetry:.*docked=1'
        $consoleDrag = Get-Coord $consoleLine 'drag'
        Assert-PointInWindow $main $consoleDrag[0] $consoleDrag[1] 'Console tab'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($consoleDrag[0]), [int][Math]::Round($consoleDrag[1]))
        $activityHiddenLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1 visible=0' $before 15
        $afterHidden = Get-LogCount
        $activityHiddenProgressLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1 visible=0' $afterHidden 15
        Assert-ActivityProgress $activityHiddenLine $activityHiddenProgressLine 'the product panel remained hidden behind another dock tab'

        $activityDrag = Get-Coord $activityHiddenProgressLine 'drag'
        Assert-PointInWindow $main $activityDrag[0] $activityDrag[1] 'hidden Background Activity tab'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($activityDrag[0]), [int][Math]::Round($activityDrag[1]))
        $activitySelectedLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1 visible=1' $before 15
        Assert-ActivityProgress $activityHiddenProgressLine $activitySelectedLine 'the hidden product tab was reselected'

        $activityDrag = Get-Coord $activitySelectedLine 'drag'
        $before = Get-LogCount
        [NativeUiTestNative]::Drag([int][Math]::Round($activityDrag[0]), [int][Math]::Round($activityDrag[1]), $targetX, $targetY)
        $activityDetachedLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=0 visible=1' $before 15
        $activityWindow = Find-Window 'Background Activity' 'Background Activity'
        if ($activityWindow -eq [IntPtr]::Zero) { throw 'Background Activity did not create its detached native host.' }
        Place-WindowInsideScreen $activityWindow $secondary 'detached Background Activity host'
        $afterDetach = Get-LogCount
        $activityDetachedProgressLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=0 visible=1' $afterDetach 15
        Assert-ActivityProgress $activityDetachedLine $activityDetachedProgressLine 'the product panel remained detached'

        $activityDrag = Get-Coord $activityDetachedProgressLine 'drag'
        $panel0Window = Get-Rect (Latest-LogMatch 'Editor native UI test telemetry: panel=0') 'window'
        $activityRedockX = [int][Math]::Round($panel0Window[0] + $panel0Window[2] * 0.5)
        $activityRedockY = [int][Math]::Round($panel0Window[1] + $panel0Window[3] * 0.5)
        Raise-NativeWindow $activityWindow
        $before = Get-LogCount
        [NativeUiTestNative]::Drag([int][Math]::Round($activityDrag[0]), [int][Math]::Round($activityDrag[1]),
                              $activityRedockX, $activityRedockY)
        $activityRedockedLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*open=1 docked=1' $before 15
        Wait-WindowGone 'Background Activity'
        Assert-ActivityProgress $activityDetachedProgressLine $activityRedockedLine 'the product panel was redocked'

        # Re-detach the same owned panel and repeat the contextual request on its native host.
        Focus-CommandPanel $main 1 'command tab before context detach'
        $commandDrag = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=1 focused=1') 'drag'
        $before = Get-LogCount
        [NativeUiTestNative]::Drag([int][Math]::Round($commandDrag[0]), [int][Math]::Round($commandDrag[1]), $targetX, $targetY)
        [void](Wait-NewLogMatch 'Editor native UI test command telemetry: docked=0' $before 15)
        $commandWindow = Wait-WindowReady 'Native UI command focus test' '' 'detached context command host'
        Place-WindowInsideScreen $commandWindow $secondary 'detached context command host'
        $commandRect = Get-NativeWindowRect $commandWindow 'detached context host'
        $laterCursorX = [int][Math]::Round($commandRect[0] + $commandRect[2] - 48)
        $laterCursorY = [int][Math]::Round($commandRect[1] + $commandRect[3] - 48)
        $detachedRequestResult = Request-ContextFromFocusedCommandPanel $commandWindow 0 `
            $laterCursorX $laterCursorY 'detached context request control'
        $detachedRequest = $detachedRequestResult.Line
        $before = $detachedRequestResult.Before
        $detachedSequence = Get-UIntField $detachedRequest 'sequence'
        $detachedAnchor = Get-Coord $detachedRequest 'anchor'
        $detachedPopup = Wait-NewLogMatch ('Editor native UI test context popup: sequence=' + $detachedSequence + ' ') $before 8
        $detachedRequestFrame = Get-UIntField $detachedRequest 'frame'
        $detachedPopupFrame = Get-UIntField $detachedPopup 'uiFrame'
        if ($detachedPopupFrame -ne ($detachedRequestFrame + 1)) { throw "Detached context popup did not open on the exact next UI frame: request=$detachedRequestFrame popup=$detachedPopupFrame." }
        $detachedPopupRect = Get-Rect $detachedPopup 'rect'
        $detachedPopupItem = Get-Coord $detachedPopup 'item'
        $detachedPopupCursor = Get-Coord $detachedPopup 'cursor'
        Assert-PointInWindow $commandWindow $detachedPopupItem[0] $detachedPopupItem[1] 'detached context popup item'
        $anchorCursorDistance = [Math]::Sqrt([Math]::Pow($detachedAnchor[0] - $detachedPopupCursor[0], 2) + [Math]::Pow($detachedAnchor[1] - $detachedPopupCursor[1], 2))
        $popupAnchorDistance = [Math]::Sqrt([Math]::Pow($detachedPopupRect[0] - $detachedAnchor[0], 2) + [Math]::Pow($detachedPopupRect[1] - $detachedAnchor[1], 2))
        $popupCursorDistance = [Math]::Sqrt([Math]::Pow($detachedPopupRect[0] - $detachedPopupCursor[0], 2) + [Math]::Pow($detachedPopupRect[1] - $detachedPopupCursor[1], 2))
        if ($anchorCursorDistance -lt 80 -or $popupAnchorDistance -ge $popupCursorDistance) {
            throw "Detached context popup did not prove request-time anchoring: anchor=$($detachedAnchor -join ',') laterCursor=$($detachedPopupCursor -join ',') popup=$($detachedPopupRect[0]),$($detachedPopupRect[1])."
        }
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($detachedPopupItem[0]), [int][Math]::Round($detachedPopupItem[1]))
        [void](Wait-NewLogMatch 'Editor native UI test command: invokes=10' $before)

        Focus-CommandPanel $commandWindow 0 'detached context cancellation tab'
        $cancelControl = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=0') 'cancel'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($cancelControl[0]), [int][Math]::Round($cancelControl[1]))
        $detachedCancelRequest = Wait-NewLogMatch 'Editor native UI test context request:.*accepted=1 cancel=1' $before 8
        $detachedCancelSequence = Get-UIntField $detachedCancelRequest 'sequence'
        [void](Wait-NewLogMatch ('Editor native UI test context cancel: sequence=' + $detachedCancelSequence + ' action=close') $before 8)
        [void](Wait-NewLogMatch ('Editor native UI test context cancel: sequence=' + $detachedCancelSequence + ' action=reopen') $before 8)
        [void](Wait-ContextPopupAbsent $detachedCancelSequence $before 'cancel=1')

        # Queue one Activity cancellation through a real mouse click on the test-owned
        # control. The control only calls the public RequestCancel path; the owner callback
        # and later producer publications remain driven by the real Activity transaction.
        $commandWindow = Wait-WindowReady 'Native UI command focus test' '' 'reopened detached Activity cancellation host'
        Focus-CommandPanel $commandWindow 0 'detached Activity cancellation control'
        $activityCancelControl = Get-Coord (Latest-LogMatch 'Editor native UI test command telemetry: docked=0 focused=1') 'activityCancel'
        Assert-PointInWindow $commandWindow $activityCancelControl[0] $activityCancelControl[1] 'Activity cancellation control'
        $before = Get-LogCount
        [NativeUiTestNative]::Click([int][Math]::Round($activityCancelControl[0]), [int][Math]::Round($activityCancelControl[1]))
        [void](Wait-NewLogMatch 'Editor native UI Activity cancel interaction:.*interactions=1 queued=1' $before 8)
        [void](Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*cancels=1 accepted=1 uiInteractions=1 queued=1 published=1.*publishedState=2 cancelQueued=1 cancelResult=1' $before 8)
        [void](Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*cancels=1 accepted=1 uiInteractions=1 queued=1 published=1.*publishedState=3' $before 8)
        $activityTerminalLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*cancels=1 accepted=1 uiInteractions=1 queued=1 published=1.*publishedState=6' $before 8
        $afterTerminal = Get-LogCount
        $activityStableTerminalLine = Wait-NewLogMatch 'Editor native UI Activity panel telemetry:.*cancels=1 accepted=1 uiInteractions=1 queued=1 published=1.*publishedState=6' $afterTerminal 8
        if ((Get-UIntField $activityTerminalLine 'cancels') -ne 1 -or
            (Get-UIntField $activityStableTerminalLine 'cancels') -ne 1 -or
            (Get-UIntField $activityStableTerminalLine 'accepted') -ne 1) {
            throw 'Background Activity cancellation owner callback was retried or duplicated.'
        }

        $main = Wait-WindowReady '' 'Vanguard Editor' 'primary editor host before shutdown'
        [NativeUiTestNative]::PostMessage($main, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        if (!$testProcess.WaitForExit(15000)) { throw 'Native UI integration-test editor did not exit after primary WM_CLOSE.' }
        $testProcess.WaitForExit()

        $interactionSummary = Latest-LogMatch 'Editor native UI test interaction summary:'
        $atlasSummary = Latest-LogMatch 'Editor native UI test minimized atlas summary:'
        $commandSummary = Latest-LogMatch 'Editor native UI test command summary:'
        $activitySummary = Latest-LogMatch 'Editor native UI Activity summary:'
        if ($interactionSummary -notmatch 'text0=1 text1=1 click0=1 click1=1 clicks0=1 clicks1=2') { throw "Native UI integration test text/click/restore check failed: $interactionSummary" }
        if ($atlasSummary -notmatch 'triggered=1 queuedWhileMinimized=1 acceptedWhileMinimized=1') { throw "Native UI integration test minimized-atlas check failed: $atlasSummary" }
        if ($commandSummary -notmatch 'dockCycle=1' -or $commandSummary -notmatch 'invokes=10 attempts=11 rejected=1' -or
            $commandSummary -notmatch 'create=1 destroy=1' -or $commandSummary -notmatch 'toolbarOffPrimary=0') {
            throw "Native UI integration test command/docking check failed: $commandSummary"
        }
        if (!$activitySummary -or $activitySummary -notmatch 'uiInteractions=1 queued=1 callbacks=1 accepted=1 admission=0 state=6') {
            throw "Native UI integration test Activity cancellation summary failed: $activitySummary"
        }
        if ((Get-UInt64Field $activitySummary 'sourceGeneration') -eq 0 -or
            (Get-UInt64Field $activitySummary 'operation') -eq 0 -or
            (Get-UIntField $activitySummary 'samples') -eq 0 -or
            (Get-UIntField $activitySummary 'producerTicks') -eq 0) {
            throw "Native UI integration test Activity source did not publish/progress: $activitySummary"
        }
        $differentDpiAvailable = $screens.Count -gt 1 -and $mainDpi -ne $commandDpi
        if ($differentDpiAvailable -and $commandSummary -notmatch 'dpiTransition=1') { throw "Native UI integration test mixed-DPI command transition was available but not observed: $commandSummary" }
        if ($differentDpiAvailable -and $interactionSummary -notmatch 'dpiTransition=1') { throw "Native UI integration test detached-panel mixed-DPI transition was available but not observed: $interactionSummary" }
        if ($testProcess.ExitCode -ne 0) { throw "Native UI integration-test editor exited with code $($testProcess.ExitCode)." }
        Write-Output $interactionSummary
        Write-Output $atlasSummary
        Write-Output $commandSummary
        Write-Output $activitySummary
        Write-Output "Native UI integration test PASS; mainDpi=$mainDpi commandDetachedDpi=$commandDpi detachedBeforeDpi=$beforeDpi detachedAfterDpi=$afterDpi screenshots=$Artifacts"
        exit 0
    }
    finally {
        if ($heartbeat) { $heartbeat.Dispose() }
        if (!$preserveProcess -and $testProcess -and !$testProcess.HasExited) { Stop-Process -Id $testProcess.Id -Force -ErrorAction SilentlyContinue }
        Remove-Item Env:VG_EDITOR_NATIVE_UI_TEST_AUTOMATION -ErrorAction SilentlyContinue
        Remove-Item Env:VG_EDITOR_NATIVE_UI_TEST_STALL_CAPTURE -ErrorAction SilentlyContinue
    }
}

# The automated panel lifecycle oracle must start from the canonical default
# presentation. Otherwise a prior run's durable per-project layout (the project
# ID is intentionally fixed across scratch directories) can hide the fixture's
# detached panel before its first Draw, even though its registration succeeds.
# Interactive persistence modes above still exercise normal saved-layout restore.
$testProcess = Start-Process -FilePath $Executable -ArgumentList ('"' + $Project + '"'), '--validate-editor-ui', '--force-default-layout' -WorkingDirectory $Artifacts -WindowStyle Hidden -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds(45)
$captured = 0
$captureAfter = [DateTime]::UtcNow.AddSeconds(4)
while (!$testProcess.HasExited -and [DateTime]::UtcNow -lt $deadline) {
    if ($captured -lt 2 -and [DateTime]::UtcNow -ge $captureAfter) { $captured = [NativeUiTestCapture]::Save([uint32]$testProcess.Id, $Artifacts) }
    Start-Sleep -Milliseconds 100
}
if (!$testProcess.HasExited) { throw "Native UI integration test timed out; process $($testProcess.Id) remains available for diagnosis." }
$testProcess.WaitForExit()
if (Test-Path -LiteralPath $log) { Get-Content $log -Tail 15 }
Write-Output "ExitCode=$($testProcess.ExitCode); capturedHosts=$captured"
exit $testProcess.ExitCode
