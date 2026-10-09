$ErrorActionPreference = 'Stop'

$taskName = 'VanguardBenchProfile'
$runner = 'E:\VanguardBench\desktop_benchmark_runner.ps1'
$userId = "$env:COMPUTERNAME\Ark"

$requiredDirectories = @(
    'E:\VanguardBench\current\ENGINE\bin\Runtime\Profile\D3D12',
    'E:\VanguardBench\current\ENGINE\games\Project Cardinal',
    'E:\VanguardBench\current\TOOLS',
    'E:\VanguardBench\captures'
)
foreach ($directory in $requiredDirectories)
{
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

if (-not (Test-Path -LiteralPath $runner))
{
    throw ('Benchmark runner is missing: ' + $runner)
}

$action = New-ScheduledTaskAction `
    -Execute 'powershell.exe' `
    -Argument ('-WindowStyle Hidden -NoProfile -ExecutionPolicy Bypass -File "' + $runner + '"')

$principal = New-ScheduledTaskPrincipal `
    -UserId $userId `
    -LogonType Interactive `
    -RunLevel Highest

$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Minutes 10)

Register-ScheduledTask `
    -TaskName $taskName `
    -Action $action `
    -Principal $principal `
    -Settings $settings `
    -Force | Out-Null

Get-ScheduledTask -TaskName $taskName |
    Select-Object TaskName, State,
        @{Name='UserId'; Expression={$_.Principal.UserId}},
        @{Name='LogonType'; Expression={$_.Principal.LogonType}},
        @{Name='RunLevel'; Expression={$_.Principal.RunLevel}} |
    Format-List
