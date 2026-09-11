param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [string]$Project,
    [Parameter(Mandatory=$true)][string]$Artifacts
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Artifacts = [IO.Path]::GetFullPath($Artifacts)
New-Item -ItemType Directory -Path $Artifacts -Force | Out-Null
if (!$Project) {
    $proofProjectDirectory = Join-Path $Artifacts 'project'
    New-Item -ItemType Directory -Path $proofProjectDirectory -Force | Out-Null
    $Project = Join-Path $proofProjectDirectory 'ui-proof.vproject'
    if (!(Test-Path -LiteralPath $Project)) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ui-proof.vproject') -Destination $Project }
}
$Project = (Resolve-Path -LiteralPath $Project).Path
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class UiProofCapture {
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
$proofProcess = Start-Process -FilePath $Executable -ArgumentList ('"' + $Project + '"'), '--validate-editor-ui' -WorkingDirectory $Artifacts -WindowStyle Hidden -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds(45)
$captured = 0
$captureAfter = [DateTime]::UtcNow.AddSeconds(4)
while (!$proofProcess.HasExited -and [DateTime]::UtcNow -lt $deadline) {
    if ($captured -lt 2 -and [DateTime]::UtcNow -ge $captureAfter) { $captured = [UiProofCapture]::Save([uint32]$proofProcess.Id, $Artifacts) }
    Start-Sleep -Milliseconds 100
}
if (!$proofProcess.HasExited) { throw "UI proof timed out; process $($proofProcess.Id) remains available for diagnosis." }
$proofProcess.WaitForExit()
Get-Content (Join-Path $Artifacts 'VanguardEditor.log') -Tail 15
Write-Output "ExitCode=$($proofProcess.ExitCode); capturedHosts=$captured"
exit $proofProcess.ExitCode
