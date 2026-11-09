# 7-Zip in the Windows 11 context menu

Puts 7-Zip's own right-click menu into the new Windows 11 context menu, the one in front of
"Show more options", at the depth the old menu had:

- **7-Zip ▸** with the commands 7-Zip offers for the selection (Open archive, Extract…, Add to…)
- **CRC SHA ▸** with 7-Zip's hash commands

The new menu shows only packaged `IExplorerCommand` handlers. 7-Zip (26.06 at the time of writing)
ships only a classic `IContextMenu` shell extension, so on its own it appears only under
"Show more options".

## How it works

`SevenZipModernMenu.dll` implements `IExplorerCommand`. For each selection it creates the installed
7-Zip shell extension (`7-zip.dll`, CLSID `{23170F69-40C1-278A-1000-000100020000}`), lets it fill a
menu and offers that menu's items; a click runs the item through 7-Zip's own `InvokeCommand`. The
menu therefore follows 7-Zip's options and language, and a 7-Zip update needs no rebuild.

Two rules of the new menu shape the layout:

- Explorer opens only one submenu level below a top-level command. "CRC SHA" is therefore a command
  of its own, and the items of "Open archive ▸" are listed as "Open archive › 7z" and so on.
- A package with more than one visible command gets an extra submenu named after the package. Each
  command therefore ships as its own package: `SevenZipModernMenu` ("7-Zip") and
  `SevenZipModernMenu.CrcSha` ("CRC SHA (7-Zip)").

## Requirements

- Windows 11 with the redesigned context menu (tested on 26H2, build 26320)
- 7-Zip x64 with "Integrate 7-Zip to shell context menu" turned on
- Developer Mode, which lets `Add-AppxPackage -Register` register the unsigned packages from a folder
- MSVC and the Windows SDK: `build.ps1` uses the private toolchain that
  `android-usb-monitor\tools\get-toolchain.ps1` installs in `%LOCALAPPDATA%\usb-monitor-toolchain`

## Build, install, remove

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1      # out\packages\SevenZip, out\packages\CrcSha, out\probe.exe
powershell -ExecutionPolicy Bypass -File install.ps1    # copies to %LOCALAPPDATA%\Programs and registers, no admin
powershell -ExecutionPolicy Bypass -File uninstall.ps1
```

Every version goes into its own folder because Explorer keeps the loaded DLL locked; raise `Version`
in `package\AppxManifest.template.xml` before installing a new build.

New entries start in the "App extensions" submenu. To show one in the main menu, turn it on in
Settings > Personalization > Context menu, which "Manage extensions" at the bottom of that submenu
also opens. Windows keeps that choice per package in
`HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ContextMenu\Preferences\extensions\<package family name>`,
value `Enabled`.

## Testing

`out\probe.exe` runs the commands outside Explorer:

```powershell
out\probe.exe --dll out\packages\SevenZip\SevenZipModernMenu.dll <path>...                  # print both menus
out\probe.exe --dll out\packages\SevenZip\SevenZipModernMenu.dll --invoke "Extract Here" <archive>
out\probe.exe --legacy <path>...                                                           # also print the shell's classic menu
```

It exits with 4 when an item below a top-level command still has a submenu. For these unsigned
packages a plain `CoCreateInstance` from an unpackaged process fails with `REGDB_E_CLASSNOTREG`, while
the shell activates them, so without `--dll` only the `--legacy` listing shows the registered packages.

## Known limits

- "Show more options" lists 7-Zip twice: 7-Zip's own entry and this one.
- The packages are unsigned and depend on Developer Mode staying on.
