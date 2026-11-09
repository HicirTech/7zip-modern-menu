# Builds the bridge DLL, the package entry executable and the test probe, and lays out one package
# per top-level command in out\packages.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = $PSScriptRoot
$out = Join-Path $root 'out'
$packagesOut = Join-Path $out 'packages'
$obj = Join-Path $root 'obj'
$binaries = Join-Path $obj 'bin'
$assets = Join-Path $obj 'Assets'

# 7-Zip's classic context menu handler; its folder holds 7zFM.exe, whose icon the packages use.
$sevenZipHandlerKey = 'Registry::HKEY_CLASSES_ROOT\CLSID\{23170F69-40C1-278A-1000-000100020000}\InprocServer32'

# Explorer puts the commands of a package with more than one visible command under an extra submenu
# named after the package, so each top-level command ships as a package of its own. Clsid values
# match src\CommandIds.h.
$packages = @(
    @{ Folder = 'SevenZip'; Name = 'SevenZipModernMenu'; DisplayName = '7-Zip'; VerbId = 'SevenZip'
        Clsid = '7B6E4117-EFEB-47AA-9E27-2DFFD7F17E92' }
    @{ Folder = 'CrcSha'; Name = 'SevenZipModernMenu.CrcSha'; DisplayName = 'CRC SHA (7-Zip)'; VerbId = 'SevenZipHash'
        Clsid = 'E0D7DE7F-98E2-4129-9FB7-A334CF2DBD8B' }
)

Add-Type -AssemblyName System.Drawing
Add-Type -Namespace SevenZipModernMenu -Name NativeMethods -MemberDefinition @'
[DllImport("shell32.dll", CharSet = CharSet.Unicode)]
public static extern int SHDefExtractIconW(string iconFile, int iconIndex, uint flags, out IntPtr largeIcon, out IntPtr smallIcon, uint iconSizes);
[DllImport("user32.dll")]
public static extern bool DestroyIcon(IntPtr icon);
'@

function Use-Toolchain {
    # Private MSVC and Windows SDK installed by android-usb-monitor\tools\get-toolchain.ps1.
    $toolchain = Join-Path $env:LOCALAPPDATA 'usb-monitor-toolchain'
    if (-not (Test-Path -LiteralPath (Join-Path $toolchain 'msvc\.done'))) {
        throw "No MSVC toolchain in $toolchain."
    }
    $msvc = (Get-ChildItem -LiteralPath (Join-Path $toolchain 'msvc\VC\Tools\MSVC') -Directory |
        Sort-Object Name | Select-Object -Last 1).FullName
    $sdk = Join-Path $toolchain 'nuget\microsoft.windows.sdk.cpp\c'
    $sdkLibs = Join-Path $toolchain 'nuget\microsoft.windows.sdk.cpp.x64\c'
    $kit = (Get-ChildItem -LiteralPath (Join-Path $sdk 'Include') -Directory |
        Sort-Object Name | Select-Object -Last 1).Name

    $env:PATH = "$msvc\bin\Hostx64\x64;$sdk\bin\$kit\x64;$env:PATH"
    $env:INCLUDE = @("$msvc\include", "$sdk\Include\$kit\ucrt", "$sdk\Include\$kit\um", "$sdk\Include\$kit\shared",
        "$sdk\Include\$kit\winrt") -join ';'
    $env:LIB = @("$msvc\lib\x64", "$sdkLibs\ucrt\x64", "$sdkLibs\um\x64") -join ';'
    # Record only the PDB file name in the binaries, not this machine's path.
    $env:_LINK_ = '/PDBALTPATH:%_PDB%'
}

function Invoke-Native([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE." }
}

function Save-IconPng([string]$IconFile, [int]$Size, [string]$Path) {
    $large = [IntPtr]::Zero
    $small = [IntPtr]::Zero
    # Low word: size of the large icon; high word: size of the small one.
    $sizes = [uint32]($Size -bor (16 -shl 16))
    $result = [SevenZipModernMenu.NativeMethods]::SHDefExtractIconW($IconFile, 0, 0, [ref]$large, [ref]$small, $sizes)
    if ($result -ne 0) { throw ('Cannot read the icon of {0} (0x{1:X8}).' -f $IconFile, $result) }
    try {
        $bitmap = [System.Drawing.Icon]::FromHandle($large).ToBitmap()
        try { $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png) } finally { $bitmap.Dispose() }
    } finally {
        [void][SevenZipModernMenu.NativeMethods]::DestroyIcon($large)
        [void][SevenZipModernMenu.NativeMethods]::DestroyIcon($small)
    }
}

Use-Toolchain
New-Item -ItemType Directory -Force -Path $out, $binaries, $assets | Out-Null

$compile = @('/nologo', '/std:c++20', '/permissive-', '/utf-8', '/W4', '/EHsc', '/O2', '/MT', '/Zi',
    '/DUNICODE', '/D_UNICODE', '/DNOMINMAX')
$link = @('/link', '/DEBUG', '/OPT:REF', '/OPT:ICF')

# Object files and PDBs stay in obj; the packages get only the files they need.
Push-Location -LiteralPath $obj
try {
    Invoke-Native -Command 'cl.exe' -Arguments ($compile + @('/LD', "/Fe$binaries\SevenZipModernMenu.dll",
        "$root\src\Module.cpp", "$root\src\ExplorerCommands.cpp", "$root\src\SevenZipMenu.cpp") + $link + @(
        "/DEF:$root\src\SevenZipModernMenu.def", "/PDB:$obj\SevenZipModernMenu.pdb",
        "/IMPLIB:$obj\SevenZipModernMenu.lib", 'ole32.lib', 'shell32.lib', 'shlwapi.lib', 'user32.lib',
        'advapi32.lib', 'uuid.lib'))
    Invoke-Native -Command 'cl.exe' -Arguments ($compile + @("/Fe$binaries\SevenZipModernMenu.exe",
        "$root\src\PackageEntry.cpp") + $link + @('/SUBSYSTEM:WINDOWS', "/PDB:$obj\SevenZipModernMenuEntry.pdb"))
    Invoke-Native -Command 'cl.exe' -Arguments ($compile + @("/Fe$out\probe.exe", "$root\test\Probe.cpp") + $link +
        @("/PDB:$obj\probe.pdb", 'ole32.lib', 'shell32.lib', 'user32.lib'))
} finally {
    Pop-Location
}

$fileManager = Join-Path (Split-Path -Parent (Get-Item -LiteralPath $sevenZipHandlerKey).GetValue('')) '7zFM.exe'
Save-IconPng -IconFile $fileManager -Size 50 -Path (Join-Path $assets 'StoreLogo.png')
Save-IconPng -IconFile $fileManager -Size 44 -Path (Join-Path $assets 'Square44x44Logo.png')
Save-IconPng -IconFile $fileManager -Size 150 -Path (Join-Path $assets 'Square150x150Logo.png')

$template = Get-Content -LiteralPath (Join-Path $root 'package\AppxManifest.template.xml') -Raw
foreach ($package in $packages) {
    $folder = Join-Path $packagesOut $package.Folder
    New-Item -ItemType Directory -Force -Path (Join-Path $folder 'Assets') | Out-Null
    Copy-Item -Path "$binaries\SevenZipModernMenu.dll", "$binaries\SevenZipModernMenu.exe" -Destination $folder
    Copy-Item -Path "$assets\*" -Destination (Join-Path $folder 'Assets')

    $manifest = $template
    foreach ($field in 'Name', 'DisplayName', 'VerbId', 'Clsid') {
        $manifest = $manifest.Replace("{$field}", $package[$field])
    }
    [IO.File]::WriteAllText((Join-Path $folder 'AppxManifest.xml'), $manifest, (New-Object Text.UTF8Encoding $false))
    "Built $folder"
}
