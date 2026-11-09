# Copies each built package to %LOCALAPPDATA%\Programs and registers it for the current user.
# Needs Developer Mode, which allows registering an unsigned package from a folder; no admin rights.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$appModel = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock' -ErrorAction SilentlyContinue
if (-not $appModel -or $appModel.AllowDevelopmentWithoutDevLicense -ne 1) {
    throw 'Developer Mode is off (Settings > System > Advanced > Developer Mode).'
}

foreach ($source in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'out\packages') -Directory) {
    $identity = ([xml](Get-Content -LiteralPath (Join-Path $source.FullName 'AppxManifest.xml') -Raw)).Package.Identity

    # Explorer keeps the DLL loaded, so every version gets its own folder.
    $installRoot = Join-Path $env:LOCALAPPDATA "Programs\$($identity.Name)"
    $target = Join-Path $installRoot $identity.Version
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item -Path (Join-Path $source.FullName '*') -Destination $target -Recurse -Force

    Add-AppxPackage -Register (Join-Path $target 'AppxManifest.xml')

    $installed = Get-AppxPackage -Name $identity.Name
    if (-not $installed -or $installed.InstallLocation -ne $target) {
        throw "$($identity.Name) is not registered from $target."
    }
    "Registered $($installed.PackageFullName) from $target"

    # Folders of earlier versions are no longer registered. Explorer may still hold an old DLL until
    # it unloads it; such a folder stays and goes on a later run.
    Get-ChildItem -LiteralPath $installRoot -Directory |
        Where-Object { $_.FullName -ne $target } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force -ErrorAction SilentlyContinue }
}
