#pragma once

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <string>
#include <vector>

// One item of 7-Zip's classic context menu, read out of the menu 7-Zip fills.
struct MenuEntry
{
    enum class Kind
    {
        Command,
        Submenu,
        Separator,
    };

    Kind kind = Kind::Command;
    std::wstring title;               // 7-Zip's text, '&' access key markers removed
    std::wstring titlePrefix;         // path of the submenus this item was lifted out of, e.g. "Open archive › "
    std::wstring verb;                // 7-Zip's verb for the item, e.g. "SevenZip.Checksum"
    UINT commandOffset = 0;           // Kind::Command: offset for IContextMenu::InvokeCommand
    bool enabled = true;
    std::vector<MenuEntry> children;  // Kind::Submenu

    std::wstring DisplayTitle() const { return titlePrefix + title; }
};

// The top-level commands of the new menu: "7-Zip", and "CRC SHA" next to it. Explorer shows only
// one submenu level below a top-level command, so 7-Zip's "CRC SHA" submenu gets its own command.
enum class MenuSlot
{
    Main,
    Hash,
};

// What one top-level command shows: its title and its submenu.
struct MenuGroup
{
    std::wstring title;
    std::vector<MenuEntry> entries;
};

// 7-Zip's classic shell extension (7-zip.dll), set up for one selection.
class SevenZipMenu
{
public:
    SevenZipMenu() = default;
    SevenZipMenu(const SevenZipMenu&) = delete;
    SevenZipMenu& operator=(const SevenZipMenu&) = delete;
    ~SevenZipMenu();

    HRESULT Load(IShellItemArray* selection);
    const std::vector<MenuEntry>& Entries() const noexcept { return entries_; }
    HRESULT Invoke(UINT commandOffset, HWND owner) const;

private:
    Microsoft::WRL::ComPtr<IContextMenu> handler_;
    HMENU menu_ = nullptr;
    std::vector<MenuEntry> entries_;
};

// Picks what a top-level command shows from 7-Zip's top-level items; false when it shows nothing.
// The entries of the group have no submenus.
bool SelectGroup(const std::vector<MenuEntry>& topLevel, MenuSlot slot, MenuGroup& group);

// Runs one command for the selection through a newly loaded 7-Zip menu.
HRESULT InvokeSevenZipCommand(IShellItemArray* selection, const MenuEntry& command, HWND owner);

// Icon of 7-Zip File Manager next to the registered 7-zip.dll; empty when 7-Zip is not registered.
std::wstring SevenZipIconLocation();
