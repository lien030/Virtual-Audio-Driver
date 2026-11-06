[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$demo = Join-Path $PSScriptRoot 'virtual-mic-demo.exe'
$processes = [Collections.Generic.List[Diagnostics.Process]]::new()

function Start-Demo([string]$Arguments, [string]$Name) {
    $process = Start-Process -FilePath $demo -ArgumentList $Arguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $PSScriptRoot "$Name.log") `
        -RedirectStandardError (Join-Path $PSScriptRoot "$Name.error.log")
    # Retain the native handle so Windows PowerShell can read the exit code after exit.
    $null = $process.Handle
    $processes.Add($process)
    return $process
}

function Complete-Demo([Diagnostics.Process]$Process, [string]$Name) {
    if (-not $Process.WaitForExit(15000)) { throw "$Name timed out." }
    $Process.WaitForExit()
    if ($Process.ExitCode -ne 0) {
        throw "$Name failed: $(Get-Content (Join-Path $PSScriptRoot "$Name.error.log") -Raw)"
    }
}

$testExit = 1
Start-Transcript -Path (Join-Path $PSScriptRoot 'lifecycle.log') -Force
try {
    $writer = Start-Demo 'tone --rate 48000 --bits 32 --channels 2 --seconds 60' 'lifecycle-writer'
    Start-Sleep -Milliseconds 300
    $first = Start-Demo "capture `"$PSScriptRoot\restart-44100.wav`" --rate 44100 --bits 16 --channels 1 --seconds 2" 'restart-44100'
    Complete-Demo $first 'restart-44100'
    if ($writer.HasExited) { throw 'Writer exited before the capture format changed.' }
    $second = Start-Demo "capture `"$PSScriptRoot\restart-192000.wav`" --rate 192000 --bits 24 --channels 2 --seconds 2" 'restart-192000'
    Complete-Demo $second 'restart-192000'
    Write-Output 'PASS: one writer survived capture stop/start and format changes.'

    $capture = Start-Demo "capture `"$PSScriptRoot\writer-reconnect.wav`" --shared --seconds 5" 'writer-reconnect'
    Start-Sleep -Seconds 1
    Stop-Process -Id $writer.Id -Force
    $writer.WaitForExit()
    $replacement = Start-Demo 'tone --rate 44100 --bits 16 --channels 2 --seconds 2' 'replacement-writer'
    Complete-Demo $replacement 'replacement-writer'
    Complete-Demo $capture 'writer-reconnect'
    Write-Output 'PASS: a new writer connected after the previous writer was terminated.'
    $status = Start-Demo 'status' 'lifecycle-status'
    Complete-Demo $status 'lifecycle-status'
    $testExit = 0
} finally {
    foreach ($process in $processes) {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        $process.Dispose()
    }
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'lifecycle.exit') -Value $testExit
    Stop-Transcript
}
exit $testExit
