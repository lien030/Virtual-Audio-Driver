[CmdletBinding()]
param(
    [Parameter(Mandatory)][PSCredential]$GuestCredential,
    [Parameter(Mandatory)][string]$VmPath,
    [Parameter(Mandatory)][string]$RustRoot,
    [switch]$Matrix
)
$ErrorActionPreference = 'Stop'
$driverRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'Test-VirtualMicBuffer.ps1')
$invokeVm = Join-Path $PSScriptRoot 'Invoke-TestVm.ps1'
$vm = @{ VmPath = $VmPath; GuestCredential = $GuestCredential }
$guestRoot = 'C:\VirtualMicTest'
$package = Join-Path $driverRoot 'x64\Release\package'
$demo = Join-Path $RustRoot 'target\release\virtual-mic-demo.exe'
$files = @('VirtualAudioDriver.inf', 'VirtualAudioDriver.sys', 'virtualaudiodriver.cat', 'VirtualMicTest.cer') |
    ForEach-Object { Join-Path $package $_ }
$files += @((Join-Path $PSScriptRoot 'Install-VirtualMic.ps1'), $demo,
    (Join-Path $driverRoot 'x64\Tests\vmic_stream_test.exe'), (Join-Path $driverRoot 'x64\Tests\vmic_endpoint_test.exe'))
$sdkTools = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Tools'
$devcon = Get-ChildItem -LiteralPath $sdkTools -Directory | Where-Object { $_.Name -match '^\d+\.' } |
    Sort-Object { [version]$_.Name } -Descending | ForEach-Object { Join-Path $_.FullName 'x64\devcon.exe' } |
    Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $devcon) { throw 'WDK x64 devcon.exe was not found.' }
$files += $devcon
foreach ($file in $files) { if (-not (Test-Path -LiteralPath $file)) { throw "Missing deployment file: $file" } }

& $invokeVm -Command listProcessesInGuest @vm | Out-Null
$prepareDirectory = "New-Item -ItemType Directory -Path '$guestRoot' -Force | Out-Null" + "`n" +
    "Get-ChildItem -LiteralPath '$guestRoot' -File | ForEach-Object { `$_.IsReadOnly = `$false }"
$createDirectory = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($prepareDirectory))
& $invokeVm -Command runProgramInGuest @vm -ArgumentList @(
    '-interactive', '-activeWindow', 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe',
    '-NoProfile', '-EncodedCommand', $createDirectory)
foreach ($file in $files) {
    & $invokeVm -Command copyFileFromHostToGuest @vm -ArgumentList @($file, (Join-Path $guestRoot (Split-Path -Leaf $file)))
}
$guestScript = @'
$ErrorActionPreference = 'Stop'
Start-Transcript -Path C:\VirtualMicTest\install.log -Force
try {
    & C:\VirtualMicTest\Install-VirtualMic.ps1 -PackagePath C:\VirtualMicTest -DevconPath C:\VirtualMicTest\devcon.exe
    & C:\VirtualMicTest\virtual-mic-demo.exe status
    if ($LASTEXITCODE -ne 0) { throw 'Driver status query failed.' }
    & C:\VirtualMicTest\vmic_stream_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'WaveRT stream regression failed.' }
    & C:\VirtualMicTest\vmic_endpoint_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Endpoint mute or shared-mode regression failed.' }
} finally { Stop-Transcript }
'@
$localScript = Join-Path $driverRoot 'x64\Release\install-vm.ps1'
[IO.File]::WriteAllText($localScript, $guestScript)
& $invokeVm -Command copyFileFromHostToGuest @vm -ArgumentList @($localScript, "$guestRoot\install-vm.ps1")
$runArguments = @('-interactive', '-activeWindow', 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$guestRoot\install-vm.ps1")
& $invokeVm -Command runProgramInGuest @vm -ArgumentList $runArguments
& $invokeVm -Command copyFileFromGuestToHost @vm -ArgumentList @("$guestRoot\install.log", (Join-Path $driverRoot 'x64\Release\install-vm.log'))
$testScript = @'
$ErrorActionPreference = 'Stop'
$testExit = 1
Remove-Item -LiteralPath C:\VirtualMicTest\selftest.exit -ErrorAction SilentlyContinue
try {
    $process = Start-Process -FilePath C:\VirtualMicTest\virtual-mic-demo.exe -ArgumentList 'selftest MATRIX_ARGUMENT' -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput C:\VirtualMicTest\selftest.log -RedirectStandardError C:\VirtualMicTest\selftest.error.log
    $testExit = $process.ExitCode
} catch {
    $_ | Out-File C:\VirtualMicTest\selftest.error.log -Append
    $testExit = 1
} finally { Set-Content -LiteralPath C:\VirtualMicTest\selftest.exit -Value $testExit }
exit $testExit
'@
$testScript = $testScript.Replace('MATRIX_ARGUMENT', $(if ($Matrix) { '--matrix' } else { '' }))
[IO.File]::WriteAllText($localScript, $testScript)
& $invokeVm -Command copyFileFromHostToGuest @vm -ArgumentList @($localScript, "$guestRoot\test-vm.ps1")
# Return immediately so the caller can inspect progress while the matrix runs.
$testArguments = @('-noWait', '-interactive', '-activeWindow', 'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$guestRoot\test-vm.ps1")
& $invokeVm -Command runProgramInGuest @vm -ArgumentList $testArguments
"VM deployment complete. Test log: $guestRoot\selftest.log"
