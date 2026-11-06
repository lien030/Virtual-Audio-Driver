[CmdletBinding()]
param([ValidateRange(5, 3600)][int]$Seconds = 120)
$ErrorActionPreference = 'Stop'
$demo = Join-Path $PSScriptRoot 'virtual-mic-demo.exe'
$testExit = 1
Start-Transcript -Path (Join-Path $PSScriptRoot 'continuous.log') -Force
try {
    foreach ($format in @(@(44100,16,1), @(192000,24,2))) {
        $name = 'continuous-' + $format[0]
        $formatArguments = "--rate $($format[0]) --bits $($format[1]) --channels $($format[2])"
        $captureArguments = "capture `"$PSScriptRoot\$name.wav`" $formatArguments --seconds $($Seconds + 2)"
        $capture = Start-Process $demo -ArgumentList $captureArguments -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput (Join-Path $PSScriptRoot "$name-capture.log") `
            -RedirectStandardError (Join-Path $PSScriptRoot "$name-capture.error.log")
        $null = $capture.Handle
        $writer = $null
        try {
            Start-Sleep -Milliseconds 400
            $writer = Start-Process $demo -ArgumentList "tone --strict $formatArguments --seconds $Seconds" -PassThru -WindowStyle Hidden `
                -RedirectStandardOutput (Join-Path $PSScriptRoot "$name-writer.log") `
                -RedirectStandardError (Join-Path $PSScriptRoot "$name-writer.error.log")
            $null = $writer.Handle
            if (-not $writer.WaitForExit(($Seconds + 10) * 1000)) { throw "$name writer timed out." }
            $writer.WaitForExit()
            if ($writer.ExitCode -ne 0) { throw "$name writer failed: $($writer.ExitCode)" }
            if (-not $capture.WaitForExit(10000)) { throw "$name capture timed out." }
            $capture.WaitForExit()
            if ($capture.ExitCode -ne 0) { throw "$name capture failed: $($capture.ExitCode)" }
            Write-Output "PASS: $Seconds seconds of $formatArguments. WAV ready for signal analysis."
        } finally {
            if ($writer) {
                if (-not $writer.HasExited) { Stop-Process -Id $writer.Id -Force }
                $writer.Dispose()
            }
            if (-not $capture.HasExited) { Stop-Process -Id $capture.Id -Force }
            $capture.Dispose()
        }
    }
    $testExit = 0
} finally {
    Set-Content -LiteralPath (Join-Path $PSScriptRoot 'continuous.exit') -Value $testExit
    Stop-Transcript
}
exit $testExit
