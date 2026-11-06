[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$TestSign
)
$ErrorActionPreference = 'Stop'
$driverRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsRoot) { throw 'Visual Studio MSBuild was not found.' }
$msbuild = Join-Path $vsRoot 'MSBuild\Current\Bin\amd64\MSBuild.exe'
& $msbuild (Join-Path $driverRoot 'VirtualAudioDriver.sln') /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 /p:SignMode=Off /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'Driver build failed.' }
$package = Join-Path $driverRoot "x64\$Configuration\package"
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Tools') -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'x64\infverif.exe') } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $sdkVersion) { throw 'WDK INF verifier was not found.' }
& (Join-Path $sdkVersion.FullName 'x64\infverif.exe') /w (Join-Path $package 'VirtualAudioDriver.inf')
if ($LASTEXITCODE -ne 0) { throw 'INF verification failed.' }

if ($TestSign) {
    $certificate = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
        Where-Object { $_.Subject -eq 'CN=Virtual Mic Development' -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(1) } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $certificate) {
        $certificateOptions = @{
            Type = 'CodeSigningCert'; Subject = 'CN=Virtual Mic Development'
            CertStoreLocation = 'Cert:\CurrentUser\My'; HashAlgorithm = 'SHA256'
            KeyAlgorithm = 'RSA'; KeyLength = 2048; NotAfter = (Get-Date).AddYears(2)
        }
        $certificate = New-SelfSignedCertificate @certificateOptions
    }
    $signTool = Join-Path $sdkRoot "bin\$($sdkVersion.Name)\x64\signtool.exe"
    & $signTool sign /sha1 $certificate.Thumbprint /s My /fd SHA256 (Join-Path $package 'VirtualAudioDriver.sys')
    if ($LASTEXITCODE -ne 0) { throw 'SYS signing failed.' }
    & (Join-Path $sdkRoot "bin\$($sdkVersion.Name)\x86\Inf2Cat.exe") "/driver:$package" /os:10_X64 /uselocaltime
    if ($LASTEXITCODE -ne 0) { throw 'Catalog generation failed.' }
    & $signTool sign /sha1 $certificate.Thumbprint /s My /fd SHA256 (Join-Path $package 'virtualaudiodriver.cat')
    if ($LASTEXITCODE -ne 0) { throw 'Catalog signing failed.' }
    Export-Certificate -Cert $certificate -FilePath (Join-Path $package 'VirtualMicTest.cer') | Out-Null
}
Get-ChildItem -LiteralPath $package | Select-Object Name, Length
