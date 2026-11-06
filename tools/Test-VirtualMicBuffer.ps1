$ErrorActionPreference = 'Stop'
$driverRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'MSVC compiler was not found.' }
$vcRoot = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory |
    Where-Object { $_.Name -match '^\d+\.' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$bin = Join-Path $vcRoot.FullName 'bin\Hostx64\x64'
$output = Join-Path $driverRoot 'x64\Tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$originalPath = $env:Path
$originalInclude = $env:INCLUDE
$originalLib = $env:LIB
try {
    $env:Path = "$bin;$env:Path"
    $env:INCLUDE = "$($vcRoot.FullName)\include;$($sdk.FullName)\ucrt;$($sdk.FullName)\shared;$($sdk.FullName)\um"
    $env:LIB = "$($vcRoot.FullName)\lib\x64;$sdkRoot\Lib\$($sdk.Name)\ucrt\x64;$sdkRoot\Lib\$($sdk.Name)\um\x64"
    $exe = Join-Path $output 'vmic_buffer_test.exe'
    & (Join-Path $bin 'cl.exe') /nologo /EHsc /W4 /WX (Join-Path $driverRoot 'Tests\vmic_buffer_test.cpp') "/Fe:$exe" "/Fo:$output\vmic_buffer_test.obj"
    if ($LASTEXITCODE -ne 0) { throw 'Buffer test compilation failed.' }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw 'Buffer self-check failed.' }
    'PASS: ring-buffer, silence, formats, 10-minute frame counts, QPC conversion, and notification periods.'
    $streamExe = Join-Path $output 'vmic_stream_test.exe'
    & (Join-Path $bin 'cl.exe') /nologo /EHsc /W4 /WX /MT (Join-Path $driverRoot 'Tests\vmic_stream_test.cpp') `
        "/Fe:$streamExe" "/Fo:$output\vmic_stream_test.obj" /link setupapi.lib ksuser.lib
    if ($LASTEXITCODE -ne 0) { throw 'Stream test compilation failed.' }
    "Built $streamExe; run it in the Windows test VM with the driver installed."
} finally {
    $env:Path = $originalPath
    $env:INCLUDE = $originalInclude
    $env:LIB = $originalLib
}
