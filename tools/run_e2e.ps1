param(
    [int]$Port = 17777,
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$effectivePath = $env:Path
Remove-Item Env:Path -ErrorAction SilentlyContinue
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:PATH = $effectivePath
$root = Split-Path -Parent $PSScriptRoot
$binary = Join-Path $root "build\bin\$Configuration"
$server = Join-Path $binary 'PortfolioServer.exe'
$client = Join-Path $binary 'DummyClient.exe'
if (!(Test-Path -LiteralPath $server) -or !(Test-Path -LiteralPath $client)) {
    throw 'Run tools/build.ps1 first.'
}

$env:PORTFOLIO_DEMO_ACCOUNT = 'portfolio-user'
$env:PORTFOLIO_DEMO_PASSWORD = 'ephemeral-e2e-password'
$output = Join-Path $root 'local-evidence\standalone-e2e'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$serverOut = Join-Path $output 'server.stdout.log'
$serverErr = Join-Path $output 'server.stderr.log'
$process = Start-Process -FilePath $server -ArgumentList "--port=$Port",'--workers=4','--auth-mode=memory' `
    -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr -WindowStyle Hidden -PassThru

try {
    $ready = $false
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        Start-Sleep -Milliseconds 100
        if ($process.HasExited) { throw 'Server exited before readiness.' }
        $probe = Test-NetConnection -ComputerName 127.0.0.1 -Port $Port -WarningAction SilentlyContinue
        if ($probe.TcpTestSucceeded) { $ready = $true; break }
    }
    if (!$ready) { throw 'Server readiness timeout.' }

    $scenarios = @('e2e','split2','split5','merge3','sizezero','oversize','partial-close','e2e')
    $results = foreach ($scenario in $scenarios) {
        $text = & $client "--port=$Port" '--account=portfolio-user' "--scenario=$scenario"
        if ($LASTEXITCODE -ne 0) { throw "Scenario failed: $scenario`n$text" }
        $text | ConvertFrom-Json
    }
    $summary = [ordered]@{
        result = 'PASS'
        scenarios = $results.Count
        passed = @($results | Where-Object result -eq 'PASS').Count
        server_alive_after_faults = !$process.HasExited
        results = $results
    }
    $summary | ConvertTo-Json -Depth 5
    $summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8
}
finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id }
    Remove-Item Env:PORTFOLIO_DEMO_ACCOUNT -ErrorAction SilentlyContinue
    Remove-Item Env:PORTFOLIO_DEMO_PASSWORD -ErrorAction SilentlyContinue
}
