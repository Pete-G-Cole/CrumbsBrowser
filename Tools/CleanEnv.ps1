$envKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'

# Get all machine-level environment variable names
$varNames = (Get-Item $envKey).Property

# Filter for names starting with "crum"
$matches = $varNames | Where-Object { $_ -like 'crum*' }

foreach ($name in $matches) {
    Write-Host "Removing machine environment variable: $name"
    Remove-ItemProperty -Path $envKey -Name $name -ErrorAction Stop
}

# Broadcast environment change
$signature = @"
using System;
using System.Runtime.InteropServices;
public class NativeMethods {
  [DllImport("user32.dll", SetLastError=true, CharSet=CharSet.Auto)]
  public static extern IntPtr SendMessageTimeout(
    IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam,
    uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
}
"@

Add-Type $signature

$HWND_BROADCAST = [IntPtr]0xffff
$WM_SETTINGCHANGE = 0x1A
$SMTO_ABORTIFHUNG = 0x0002
$result = [UIntPtr]::Zero

[NativeMethods]::SendMessageTimeout(
    $HWND_BROADCAST,
    $WM_SETTINGCHANGE,
    [UIntPtr]::Zero,
    'Environment',
    $SMTO_ABORTIFHUNG,
    5000,
    [ref]$result
)
