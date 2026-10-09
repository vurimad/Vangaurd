param(
    [Parameter(Mandatory=$true)][string]$NativeExecutable,
    [Parameter(Mandatory=$true)][string]$LifecycleExecutable,
    [string]$Project,
    [Parameter(Mandatory=$true)][string]$Artifacts
)

$ErrorActionPreference = 'Stop'
$NativeExecutable = (Resolve-Path -LiteralPath $NativeExecutable).Path
$LifecycleExecutable = (Resolve-Path -LiteralPath $LifecycleExecutable).Path
$Artifacts = [IO.Path]::GetFullPath($Artifacts)
New-Item -ItemType Directory -Path $Artifacts -Force | Out-Null

if (!$Project) {
    $Project = Join-Path $PSScriptRoot 'native-ui-integration-test.vproject'
}
$Project = (Resolve-Path -LiteralPath $Project).Path

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function New-OwnedProject([string]$Name) {
    $root = Join-Path $Artifacts $Name
    if (Test-Path -LiteralPath $root) {
        Remove-Item -LiteralPath $root -Recurse -Force
    }
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $projectPath = Join-Path $root 'native-ui-integration-test.vproject'
    Copy-Item -LiteralPath $Project -Destination $projectPath
    foreach ($relative in @('Assets','DerivedData','Intermediate','Saved','Builds','Config','Plugins')) {
        New-Item -ItemType Directory -Path (Join-Path $root $relative) -Force | Out-Null
    }
    return [PSCustomObject]@{
        Root = $root
        Project = $projectPath
        Log = (Join-Path $root 'Saved/Logs/VanguardEditor.log')
        Canonical = (Join-Path $root 'Saved/Editor/user-state.vstate')
    }
}

function Assert-TestPluginPresent([string]$ProjectPath) {
    $text = [IO.File]::ReadAllText($ProjectPath)
    if ($text -notmatch '(?m)^\s*plugin\s*=\s*"editor-native-ui-integration-test"\s*$') {
        throw 'The persistence fixture project must select editor-native-ui-integration-test.'
    }
}

function Set-TestPluginSelected([string]$ProjectPath, [bool]$Selected) {
    $text = [IO.File]::ReadAllText($ProjectPath)
    $pattern = '(?m)^\s*plugin\s*=\s*"editor-native-ui-integration-test"\s*\r?\n?'
    $hasPlugin = [regex]::IsMatch($text, $pattern)
    if ($Selected -and !$hasPlugin) {
        $anchor = '(?m)^(target\s*=\s*"[^"]+"\s*\r?\n)'
        if (![regex]::IsMatch($text, $anchor)) {
            throw 'Could not find target line while restoring the persistence test plugin selection.'
        }
        $text = [regex]::Replace(
            $text, $anchor,
            { param($m) $m.Groups[1].Value + 'plugin = "editor-native-ui-integration-test"' + [Environment]::NewLine },
            1)
    }
    elseif (!$Selected -and $hasPlugin) {
        $text = [regex]::Replace($text, $pattern, '', 1)
    }
    Write-Utf8NoBom $ProjectPath $text
}

function Set-ProjectId([string]$ProjectPath, [string]$Id) {
    $text = [IO.File]::ReadAllText($ProjectPath)
    $pattern = '(?m)^project\.id\s*=\s*"[^"]+"\s*$'
    if (![regex]::IsMatch($text, $pattern)) {
        throw 'Could not find project.id in the persistence fixture project.'
    }
    $text = [regex]::Replace($text, $pattern, ('project.id = "' + $Id + '"'), 1)
    Write-Utf8NoBom $ProjectPath $text
}

function Invoke-EditorPhase([string]$Label, [string]$Executable, [string]$ProjectPath,
                            [string[]]$Flags, [string]$LogPath) {
    if (Test-Path -LiteralPath $LogPath) {
        Remove-Item -LiteralPath $LogPath -Force
    }
    $arguments = @($ProjectPath) + $Flags
    $process = Start-Process -FilePath $Executable -ArgumentList $arguments -WorkingDirectory $Artifacts -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit(45000)) {
        throw "$Label did not exit within the proof timeout; process $($process.Id) is left available for diagnosis."
    }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "$Label failed with exit code $($process.ExitCode)."
    }
    if (!(Test-Path -LiteralPath $LogPath -PathType Leaf)) {
        throw "$Label did not produce the expected editor log."
    }
    $text = [IO.File]::ReadAllText($LogPath)
    if ($text -notmatch 'Editor persistence lifecycle event:.*stage=7 state=13 result=0') {
        throw "$Label did not reach clean service shutdown."
    }
    if ($text -match 'editor frame failed:|application state failure:|application runner failed:') {
        throw "$Label reported an editor frame/application-state failure."
    }
    Copy-Item -LiteralPath $LogPath -Destination (Join-Path $Artifacts ($Label + '.log')) -Force
    Write-Output "Persistence phase PASS: $Label"
}

function Get-CanonicalHash([string]$Path) {
    if (!(Test-Path -LiteralPath $Path)) {
        throw "Expected canonical user-state file is missing: $Path"
    }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

$fixture = New-OwnedProject 'persistence-project'
Assert-TestPluginPresent $fixture.Project
$editorStateRoot = Join-Path $fixture.Root 'Saved/Editor'
if (Test-Path -LiteralPath $editorStateRoot) {
    Remove-Item -LiteralPath $editorStateRoot -Recurse -Force
}

Invoke-EditorPhase '01-seed' $NativeExecutable $fixture.Project @('--validate-editor-ui','--persistence-seed') $fixture.Log
$baselineHash = Get-CanonicalHash $fixture.Canonical
Copy-Item -LiteralPath $fixture.Canonical -Destination (Join-Path $Artifacts 'baseline-user-state.vstate') -Force

Invoke-EditorPhase '02-restore' $NativeExecutable $fixture.Project @('--validate-editor-ui','--persistence-restore') $fixture.Log

Set-TestPluginSelected $fixture.Project $false
Invoke-EditorPhase '03-missing-extension' $LifecycleExecutable $fixture.Project @('--user-state-missing-path') $fixture.Log
Set-TestPluginSelected $fixture.Project $true
Invoke-EditorPhase '04-extension-return' $NativeExecutable $fixture.Project @('--validate-editor-ui','--persistence-restore') $fixture.Log

$goodCanonical = Join-Path $Artifacts 'good-user-state-after-return.vstate'
Copy-Item -LiteralPath $fixture.Canonical -Destination $goodCanonical -Force

Copy-Item -LiteralPath $goodCanonical -Destination $fixture.Canonical -Force
Invoke-EditorPhase '05-corrupt-rejected' $NativeExecutable $fixture.Project @('--validate-editor-ui','--persistence-corrupt') $fixture.Log

Copy-Item -LiteralPath $goodCanonical -Destination $fixture.Canonical -Force
Invoke-EditorPhase '06-truncated-rejected' $NativeExecutable $fixture.Project @('--validate-editor-ui','--persistence-truncated') $fixture.Log

Copy-Item -LiteralPath $goodCanonical -Destination $fixture.Canonical -Force
Invoke-EditorPhase '07-future-rejected' $NativeExecutable $fixture.Project @('--validate-editor-ui','--persistence-future') $fixture.Log

Copy-Item -LiteralPath $goodCanonical -Destination $fixture.Canonical -Force

$projectBeforeMismatch = [IO.File]::ReadAllText($fixture.Project)
try {
    Set-ProjectId $fixture.Project '11111111-2222-4333-8444-555555555555'
    $mismatchHash = Get-CanonicalHash $fixture.Canonical
    Invoke-EditorPhase '08-project-mismatch' $NativeExecutable $fixture.Project @('--validate-editor-ui','--persistence-project-mismatch') $fixture.Log
    if ((Get-CanonicalHash $fixture.Canonical) -ne $mismatchHash) {
        throw 'Foreign-project canonical state changed during the write-inhibited run.'
    }
}
finally {
    Write-Utf8NoBom $fixture.Project $projectBeforeMismatch
}

$unwritable = New-OwnedProject 'unwritable-project'
Set-TestPluginSelected $unwritable.Project $false
$unwritableEditorPath = Join-Path $unwritable.Root 'Saved/Editor'
if (Test-Path -LiteralPath $unwritableEditorPath) {
    Remove-Item -LiteralPath $unwritableEditorPath -Recurse -Force
}
Invoke-EditorPhase '09-unwritable-editor-state' $LifecycleExecutable $unwritable.Project @('--user-state-unwritable-path') $unwritable.Log

Write-Output ("User-state persistence fixture PASS; seedHash={0}; artifacts={1}" -f $baselineHash, $Artifacts)
exit 0
