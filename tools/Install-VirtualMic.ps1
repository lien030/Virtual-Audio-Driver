[CmdletBinding()]
param([string]$PackagePath = $PSScriptRoot, [string]$DevconPath = (Join-Path $PSScriptRoot 'devcon.exe'))
$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this installer as Administrator.' }
$inf = Join-Path $PackagePath 'VirtualAudioDriver.inf'
if (-not (Test-Path -LiteralPath $inf)) { throw "INF not found: $inf" }
$testCertificate = Join-Path $PackagePath 'VirtualMicTest.cer'
if (Test-Path -LiteralPath $testCertificate) {
    Import-Certificate -FilePath $testCertificate -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Import-Certificate -FilePath $testCertificate -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
}
$existing = @(Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue |
    Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds -ErrorAction SilentlyContinue |
    Where-Object { $_.Data -contains 'ROOT\VirtualAudioDriver' })
if ($existing.Count -eq 0) {
    if (-not (Test-Path -LiteralPath $DevconPath)) { throw 'Initial root-device installation requires the WDK x64 devcon.exe beside this script.' }
    & $DevconPath install $inf 'ROOT\VirtualAudioDriver'
    if ($LASTEXITCODE -notin @(0, 1)) { throw "devcon installation failed: $LASTEXITCODE" }
} else {
    & pnputil.exe /add-driver $inf /install
    if ($LASTEXITCODE -notin @(0, 3010)) { throw "pnputil installation failed: $LASTEXITCODE" }
}
Get-PnpDevice | Where-Object { $_.InstanceId -in $existing.InstanceId -or $_.FriendlyName -like '*Virtual Mic*' } |
    Select-Object Status, Class, FriendlyName, InstanceId
