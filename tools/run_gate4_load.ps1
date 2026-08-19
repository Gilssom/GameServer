param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [Parameter(Mandatory = $true)]
    [string]$BinaryDirectory,
    [int[]]$Stages = @(1, 10, 50, 100),
    [int]$Port = 17782,
    [int]$ClientTimeoutSeconds = 90,
    [int]$StageTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'

# Codex desktop may expose both Path and PATH. Start-Process rejects the
# duplicate case-insensitive environment key, so normalize this process only.
$effectivePath = $env:Path
Remove-Item Env:Path -ErrorAction SilentlyContinue
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:PATH = $effectivePath

if ([string]::IsNullOrEmpty($env:GAMESERVER_DB_PASSWORD)) {
    throw 'GAMESERVER_DB_PASSWORD is required.'
}
if ([string]::IsNullOrEmpty($env:GAMESERVER_TEST_ACCOUNT_PASSWORD)) {
    throw 'GAMESERVER_TEST_ACCOUNT_PASSWORD is required.'
}

$serverExecutable = Join-Path $BinaryDirectory 'GameServer.exe'
$clientExecutable = Join-Path $BinaryDirectory 'DummyClient.exe'
if (!(Test-Path -LiteralPath $serverExecutable) -or
    !(Test-Path -LiteralPath $clientExecutable)) {
    throw 'Release binaries were not found.'
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$runStamp = Get-Date -Format 'MMddHHmmss'
$logicalProcessorCount = [Environment]::ProcessorCount
$stageSummaries = [System.Collections.Generic.List[object]]::new()
$stageOrdinal = 0

function Get-NearestRankPercentile {
    param([long[]]$Values, [double]$Percentile)
    if ($Values.Count -eq 0) { return 0 }
    $sorted = @($Values | Sort-Object)
    $index = [Math]::Ceiling($Percentile * $sorted.Count) - 1
    $index = [Math]::Max(0, [Math]::Min($index, $sorted.Count - 1))
    return $sorted[$index]
}

foreach ($clientCount in $Stages) {
	$stageOrdinal++
    if ($clientCount -lt 1 -or $clientCount -gt 100) {
        throw "Stage client count must be between 1 and 100: $clientCount"
    }

    $stageName = "clients-$clientCount"
    $stageDirectory = Join-Path $OutputDirectory $stageName
    New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null
    $serverOut = Join-Path $stageDirectory 'server.log'
    $serverErr = Join-Path $stageDirectory 'server.err.log'
    $env:GAMESERVER_PORT = $Port.ToString()
    $server = $null

    try {
        $server = Start-Process `
            -FilePath $serverExecutable `
            -WorkingDirectory $binaryDirectory `
            -RedirectStandardOutput $serverOut `
            -RedirectStandardError $serverErr `
            -WindowStyle Hidden `
            -PassThru
        Start-Sleep -Seconds 2
        if ($server.HasExited) {
            throw "Server failed to start for stage $stageName."
        }

        $baselineProcess = Get-Process -Id $server.Id
        $baselineWorkingSet = $baselineProcess.WorkingSet64
        $baselinePrivate = $baselineProcess.PrivateMemorySize64
        $baselineCpuSeconds = $baselineProcess.TotalProcessorTime.TotalSeconds

        $clients = [System.Collections.Generic.List[object]]::new()
        $preparationStartedAt = [DateTime]::UtcNow
        $synchronizedStartAt = $preparationStartedAt.AddSeconds(6)
        $startAtUnixMs = [DateTimeOffset]$synchronizedStartAt
        $startAtUnixMs = $startAtUnixMs.ToUnixTimeMilliseconds()
        for ($index = 1; $index -le $clientCount; $index++) {
            $label = 'client-{0:D3}' -f $index
            $jsonPath = Join-Path $stageDirectory "$label.json"
            $outPath = Join-Path $stageDirectory "$label.log"
            $errPath = Join-Path $stageDirectory "$label.err.log"
            $accountName = "g4_${clientCount}_${runStamp}_$index"
            $characterName = "G$($runStamp.Substring(4))${stageOrdinal}$index"
            $argumentLine =
                "--scenario e2e --account $accountName " +
                "--character $characterName --port $Port " +
                "--timeout $ClientTimeoutSeconds --start-at-ms $startAtUnixMs " +
                "--result `"$jsonPath`""
            $process = Start-Process `
                -FilePath $clientExecutable `
                -ArgumentList $argumentLine `
                -WorkingDirectory $binaryDirectory `
                -RedirectStandardOutput $outPath `
                -RedirectStandardError $errPath `
                -WindowStyle Hidden `
                -PassThru
            $clients.Add($process)
        }
        $launchCompletedAt = [DateTime]::UtcNow
        if ($launchCompletedAt -ge $synchronizedStartAt) {
            throw "Client launch exceeded the synchronized-start window: $stageName"
        }
        while ([DateTime]::UtcNow -lt $synchronizedStartAt) {
            Start-Sleep -Milliseconds 20
        }
		$synchronizedReadyClients = @(
			$clients | Where-Object { !$_.HasExited }
		).Count
		if ($synchronizedReadyClients -ne $clientCount) {
			throw "Not all clients reached the synchronized start: $synchronizedReadyClients/$clientCount"
		}

        $stageStartedAt = $synchronizedStartAt
        $measurementProcess = Get-Process -Id $server.Id
        $previousCpuSeconds = $measurementProcess.TotalProcessorTime.TotalSeconds
		$measurementCpuStart = $previousCpuSeconds
        $previousSampleAt = [DateTime]::UtcNow

        $samples = [System.Collections.Generic.List[object]]::new()
        $deadline = $stageStartedAt.AddSeconds($StageTimeoutSeconds)
        while ($true) {
			Start-Sleep -Milliseconds 100
            $running = @($clients | Where-Object { !$_.HasExited })
            $sampledAt = [DateTime]::UtcNow
            $serverProcess = Get-Process -Id $server.Id -ErrorAction Stop
            $cpuSeconds = $serverProcess.TotalProcessorTime.TotalSeconds
            $elapsedSeconds = ($sampledAt - $previousSampleAt).TotalSeconds
            $cpuPercent = if ($elapsedSeconds -gt 0) {
                (($cpuSeconds - $previousCpuSeconds) / $elapsedSeconds) *
                    100.0 / $logicalProcessorCount
            } else { 0.0 }
			$cpuPercent = [Math]::Max(0.0, [Math]::Min(100.0, $cpuPercent))
            $samples.Add([pscustomobject]@{
                elapsed_ms = [Math]::Round(($sampledAt - $stageStartedAt).TotalMilliseconds)
                running_clients = $running.Count
                server_cpu_percent = [Math]::Round($cpuPercent, 2)
                working_set_bytes = $serverProcess.WorkingSet64
                private_bytes = $serverProcess.PrivateMemorySize64
                handles = $serverProcess.HandleCount
                threads = $serverProcess.Threads.Count
            })
            $previousCpuSeconds = $cpuSeconds
            $previousSampleAt = $sampledAt

            if ($running.Count -eq 0) { break }
            if ($sampledAt -ge $deadline) {
                foreach ($client in $running) {
                    Stop-Process -Id $client.Id -ErrorAction SilentlyContinue
                }
                throw "Stage timeout: $stageName"
            }
        }

        foreach ($client in $clients) { $client.WaitForExit() }
        $stageCompletedAt = [DateTime]::UtcNow
        Start-Sleep -Milliseconds 300
        if ($server.HasExited) {
            throw "Server exited during stage $stageName."
        }

        $clientResults = [System.Collections.Generic.List[object]]::new()
        for ($index = 1; $index -le $clientCount; $index++) {
            $jsonPath = Join-Path $stageDirectory ('client-{0:D3}.json' -f $index)
            if (!(Test-Path -LiteralPath $jsonPath)) {
                $clientResults.Add([pscustomobject]@{
                    result = 'FAIL'; duration_ms = 0; exit_code = -1
                    message = 'Missing JSON result'
                })
                continue
            }
            $clientResults.Add((Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json))
        }

        $passed = @($clientResults | Where-Object { $_.result -eq 'PASS' }).Count
        $failed = $clientCount - $passed
        $durations = [long[]]@(
            $clientResults |
                Where-Object { $_.result -eq 'PASS' } |
                ForEach-Object { [long]$_.duration_ms }
        )
        $wallMilliseconds = [long](($stageCompletedAt - $stageStartedAt).TotalMilliseconds)
        $wallSeconds = [Math]::Max(0.001, $wallMilliseconds / 1000.0)
        $peakWorkingSet = ($samples | Measure-Object working_set_bytes -Maximum).Maximum
        $peakPrivate = ($samples | Measure-Object private_bytes -Maximum).Maximum
        $peakCpu = ($samples | Measure-Object server_cpu_percent -Maximum).Maximum
		$sampledPeakRunningClients =
			($samples | Measure-Object running_clients -Maximum).Maximum
		$peakRunningClients = [Math]::Max(
			$synchronizedReadyClients,
			$sampledPeakRunningClients)
        $peakHandles = ($samples | Measure-Object handles -Maximum).Maximum
        $peakThreads = ($samples | Measure-Object threads -Maximum).Maximum
		$finalServerProcess = Get-Process -Id $server.Id
		$serverCpuAverage =
			(($finalServerProcess.TotalProcessorTime.TotalSeconds - $measurementCpuStart) /
			$wallSeconds) * 100.0 / $logicalProcessorCount
		$serverCpuAverage = [Math]::Max(0.0, [Math]::Min(100.0, $serverCpuAverage))

        $poolValues = @(
            Get-Content -LiteralPath $serverOut |
                Select-String -Pattern 'pool_available=(\d+)' |
                ForEach-Object { [int]$_.Matches[0].Groups[1].Value }
        )
        $minimumPoolAvailable = if ($poolValues.Count -gt 0) {
            ($poolValues | Measure-Object -Minimum).Minimum
        } else { -1 }
        $poolUnavailableErrors = @(
            Get-Content -LiteralPath $serverOut |
                Select-String -SimpleMatch 'DB_POOL_UNAVAILABLE'
        ).Count

        $samples | Export-Csv `
            -LiteralPath (Join-Path $stageDirectory 'server-samples.csv') `
            -NoTypeInformation -Encoding UTF8
        $clientResults | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath (Join-Path $stageDirectory 'client-results.json') `
            -Encoding UTF8

        $summary = [pscustomobject]@{
            clients = $clientCount
            passed = $passed
            failed = $failed
            success_rate_percent = [Math]::Round(($passed * 100.0) / $clientCount, 2)
            launch_ms = [long](($launchCompletedAt - $preparationStartedAt).TotalMilliseconds)
			synchronized_ready_clients = $synchronizedReadyClients
            synchronized_peak_clients = $peakRunningClients
            wall_ms = $wallMilliseconds
            throughput_e2e_per_sec = [Math]::Round($passed / $wallSeconds, 2)
            duration_min_ms = if ($durations.Count) { ($durations | Measure-Object -Minimum).Minimum } else { 0 }
            duration_p50_ms = Get-NearestRankPercentile $durations 0.50
            duration_p95_ms = Get-NearestRankPercentile $durations 0.95
            duration_p99_ms = Get-NearestRankPercentile $durations 0.99
            duration_max_ms = if ($durations.Count) { ($durations | Measure-Object -Maximum).Maximum } else { 0 }
			logical_processors = $logicalProcessorCount
			server_cpu_average_percent = [Math]::Round($serverCpuAverage, 2)
            server_cpu_peak_percent = [Math]::Round([double]$peakCpu, 2)
            server_working_set_baseline_mb = [Math]::Round($baselineWorkingSet / 1MB, 2)
            server_working_set_peak_mb = [Math]::Round($peakWorkingSet / 1MB, 2)
            server_private_baseline_mb = [Math]::Round($baselinePrivate / 1MB, 2)
            server_private_peak_mb = [Math]::Round($peakPrivate / 1MB, 2)
            server_handles_peak = $peakHandles
            server_threads_peak = $peakThreads
            db_pool_min_available = $minimumPoolAvailable
            db_pool_unavailable_errors = $poolUnavailableErrors
            server_stderr_bytes = (Get-Item -LiteralPath $serverErr).Length
            server_alive_before_stop = !$server.HasExited
        }
        $stageSummaries.Add($summary)
        $summary | ConvertTo-Json -Depth 4 |
            Set-Content -LiteralPath (Join-Path $stageDirectory 'stage-summary.json') `
            -Encoding UTF8
    }
    finally {
        if ($server -and !$server.HasExited) {
            Stop-Process -Id $server.Id
            $server.WaitForExit()
        }
    }
}

$stageSummaries | Export-Csv `
    -LiteralPath (Join-Path $OutputDirectory 'gate4-stage-summary.csv') `
    -NoTypeInformation -Encoding UTF8
$stageSummaries | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath (Join-Path $OutputDirectory 'gate4-stage-summary.json') `
    -Encoding UTF8

$allPassed = @($stageSummaries | Where-Object { $_.failed -eq 0 }).Count -eq $Stages.Count
[pscustomobject]@{
    result = if ($allPassed) { 'PASS' } else { 'FAIL' }
    stages = $Stages
    completed_stages = $stageSummaries.Count
    all_clients_passed = $allPassed
    output_directory = $OutputDirectory
} | ConvertTo-Json -Depth 4

if (!$allPassed) { exit 1 }
