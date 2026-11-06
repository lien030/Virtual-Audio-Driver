<#
.SYNOPSIS
Runs a vmrun command on an encrypted VMware test VM, using the VM password cached in Windows Credential Manager.
.DESCRIPTION
vmrun receives the VMX path before ArgumentList. Pass GuestCredential for commands that run inside the guest.
VmrunPath defaults to the VMware Workstation installation recorded in the registry.
.EXAMPLE
.\tools\Invoke-TestVm.ps1 -VmPath <test VM .vmx> -Command getGuestIPAddress
.EXAMPLE
.\tools\Invoke-TestVm.ps1 -VmPath <test VM .vmx> -Command listProcessesInGuest -GuestCredential (Get-Credential)
#>
[CmdletBinding()]
param(
    [string]$Command = 'checkToolsState',
    [Parameter(Mandatory)][string]$VmPath,
    [string[]]$ArgumentList = @(),
    [PSCredential]$GuestCredential,
    [string]$VmrunPath
)

$ErrorActionPreference = 'Stop'
if (-not $VmrunPath) {
    $workstation = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\VMware, Inc.\VMware Workstation' -ErrorAction SilentlyContinue
    if ($workstation.InstallPath) { $VmrunPath = Join-Path $workstation.InstallPath 'vmrun.exe' }
}
if (-not $VmrunPath -or -not (Test-Path -LiteralPath $VmrunPath -PathType Leaf)) {
    throw 'vmrun.exe not found; pass -VmrunPath.'
}
$VmPath = (Resolve-Path -LiteralPath $VmPath).ProviderPath
$guidMatch = [regex]::Match(
    (Get-Content -LiteralPath $VmPath -Raw),
    '(?m)^encryptedVM\.guid\s*=\s*"([^"]+)"'
)
if (-not $guidMatch.Success) {
    throw "encryptedVM.guid not found in $VmPath"
}

if (-not ('TestVmCachedPassword' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

public static class TestVmCachedPassword
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Credential
    {
        public uint Flags, Type;
        public IntPtr TargetName, Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public uint BlobSize;
        public IntPtr Blob;
        public uint Persist, AttributeCount;
        public IntPtr Attributes, TargetAlias, UserName;
    }

    [DllImport("advapi32.dll", EntryPoint = "CredReadW", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CredRead(string target, uint type, uint flags, out IntPtr credential);
    [DllImport("advapi32.dll")]
    private static extern void CredFree(IntPtr credential);

    public static string Read(string target)
    {
        IntPtr pointer;
        if (!CredRead(target, 1, 0, out pointer))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot read cached VMware password for " + target);
        byte[] bytes = null;
        try
        {
            var credential = Marshal.PtrToStructure<Credential>(pointer);
            bytes = new byte[credential.BlobSize];
            Marshal.Copy(credential.Blob, bytes, 0, bytes.Length);
            return new UTF8Encoding(false, true).GetString(bytes);
        }
        finally
        {
            if (bytes != null) Array.Clear(bytes, 0, bytes.Length);
            CredFree(pointer);
        }
    }
}
'@
}

$vmPassword = $null
$vmrunArguments = @()
try {
    $vmPassword = [TestVmCachedPassword]::Read($guidMatch.Groups[1].Value)
    if ([string]::IsNullOrEmpty($vmPassword)) {
        throw 'The cached VMware password is empty.'
    }
    # vmrun takes both passwords only as arguments, so local process auditing can record them.
    $vmrunArguments = @('-T', 'ws', '-vp', $vmPassword)
    if ($GuestCredential) {
        $vmrunArguments += @('-gu', $GuestCredential.UserName, '-gp', $GuestCredential.GetNetworkCredential().Password)
    }
    $vmrunArguments += @($Command, $VmPath) + $ArgumentList
    & $VmrunPath @vmrunArguments
    if ($LASTEXITCODE -ne 0) {
        throw "vmrun $Command failed (exit code $LASTEXITCODE)."
    }
}
finally {
    $vmPassword = $null
    [Array]::Clear($vmrunArguments, 0, $vmrunArguments.Length)
}
