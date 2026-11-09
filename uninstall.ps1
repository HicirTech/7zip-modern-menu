# Removes the packages of this bridge and their files under %LOCALAPPDATA%\Programs.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

foreach ($installed in Get-AppxPackage -Publisher 'CN=SevenZipModernMenu') {
    Remove-AppxPackage -Package $installed.PackageFullName

    $installRoot = Join-Path $env:LOCALAPPDATA "Programs\$($installed.Name)"
    if (Test-Path -LiteralPath $installRoot) {
        # Explorer may still hold the DLL; the rest of the folder goes now, the DLL after sign-out.
        Remove-Item -LiteralPath $installRoot -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $installRoot) {
            "Explorer still uses files in $installRoot; delete the folder after signing out."
        }
    }
}
