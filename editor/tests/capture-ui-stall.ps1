param(
    [string]$Executable = 'bin/Editor/Debug/VanguardEditor.exe',
    [string]$Artifacts = 'build/e0f/hang-capture',
    [string]$Debugger = 'C:/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe',
    [ValidateRange(1,5)][int]$Captures = 3,
    [ValidateRange(250,10000)][int]$ThresholdMs = 750
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Debugger = (Resolve-Path -LiteralPath $Debugger).Path
$Artifacts = [IO.Path]::GetFullPath($Artifacts)
$runDirectory = Join-Path $Artifacts ([DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $runDirectory | Out-Null
$projectDirectory = Join-Path $runDirectory 'project'
New-Item -ItemType Directory -Path $projectDirectory | Out-Null
$project = Join-Path $projectDirectory 'ui-proof.vproject'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'ui-proof.vproject') -Destination $project
$previousCaptureSetting = $env:VG_UI_STALL_CAPTURE
try {
    $env:VG_UI_STALL_CAPTURE = '1'
    # Visible because this is the interactive editor the user explicitly needs to manipulate.
    $editorProcess = Start-Process -FilePath $Executable -ArgumentList ('"' + $project + '"'), '--inspect-editor-ui' -WorkingDirectory $runDirectory -PassThru
} finally { $env:VG_UI_STALL_CAPTURE = $previousCaptureSetting }
Write-Output "Drag/dock panels in the editor. Captures stay local in $runDirectory"
$heartbeat = $null
$startupDeadline = [DateTime]::UtcNow.AddSeconds(45)
while (!$editorProcess.HasExited -and !$heartbeat -and [DateTime]::UtcNow -lt $startupDeadline) {
    try { $heartbeat = [Threading.EventWaitHandle]::OpenExisting("Local\VanguardUiHeartbeat-$($editorProcess.Id)") }
    catch [Threading.WaitHandleCannotBeOpenedException] { Start-Sleep -Milliseconds 100 }
}
if (!$heartbeat) { throw 'Editor heartbeat unavailable. The editor was not terminated; inspect its log.' }
try {
    # Arm only after one completed frame, excluding startup and shader compilation.
    while (!$editorProcess.HasExited -and !$heartbeat.WaitOne(250)) {}
    $count = 0
    while (!$editorProcess.HasExited -and $count -lt $Captures) {
        if ($heartbeat.WaitOne($ThresholdMs)) { continue }
        if ($editorProcess.HasExited) { break }
        ++$count
        $dump = Join-Path $runDirectory "stall-$count.dmp"
        $stacks = Join-Path $runDirectory "stall-$count.txt"
        $errors = Join-Path $runDirectory "stall-$count.stderr.txt"
        $symbols = Split-Path -Parent $Executable
        $commands = '.echo UI_STALL_CAPTURE; .time; ~* kv; .dump /m \"' + $dump + '\"; .detach; q'
        # Do not pass -g: the initial attach break is the point whose stacks we need.
        $arguments = '-p ' + $editorProcess.Id + ' -y "' + $symbols + '" -c "' + $commands + '"'
        Write-Output "Capturing stall $count (this briefly pauses the editor)..."
        $capture = Start-Process -FilePath $Debugger -ArgumentList $arguments -WindowStyle Hidden -RedirectStandardOutput $stacks -RedirectStandardError $errors -PassThru
        if (!$capture.WaitForExit(15000)) {
            throw "Debugger $($capture.Id) did not finish. It may still be attached; do not kill it (that may terminate the editor). Detach it before continuing."
        }
        $detached = Select-String -LiteralPath $stacks -Pattern '^Detached$' -Quiet
        if (($null -ne $capture.ExitCode -and $capture.ExitCode -ne 0) -or !(Test-Path -LiteralPath $dump) -or !$detached) { throw "Debugger capture failed; inspect $stacks and $errors" }
        # Do not capture the same uninterrupted freeze repeatedly or count debugger delay as another stall.
        while (!$editorProcess.HasExited -and !$heartbeat.WaitOne(250)) {}
        Start-Sleep -Milliseconds 500
    }
    Write-Output "Captured $count stalls. Close the editor normally when finished; artifacts: $runDirectory"
} finally { $heartbeat.Dispose() }
