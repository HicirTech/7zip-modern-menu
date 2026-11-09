#include "SevenZipMenu.h"

#include <shlobj.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace
{
// 7-Zip's classic context menu handler, registered by the 7-Zip installer.
constexpr CLSID kSevenZipShellExtension =
    {0x23170F69, 0x40C1, 0x278A, {0x10, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00}};

// QueryContextMenu numbers the items from kFirstCommandId; InvokeCommand takes offsets from it.
constexpr UINT kFirstCommandId = 1;
constexpr UINT kLastCommandId = 0x7FFF;

// Title of the one command used when 7-Zip's "Cascaded context menu" option is off.
constexpr wchar_t kFlatMenuTitle[] = L"7-Zip";

// Verbs 7-Zip gives its "7-Zip" submenu (cascaded mode) and its "CRC SHA" submenu.
constexpr wchar_t kCascadedMenuVerb[] = L"SevenZip";
constexpr wchar_t kChecksumMenuVerb[] = L"SevenZip.Checksum";

// Joins the title of a lifted submenu with the titles of its items.
constexpr wchar_t kSubmenuSeparator[] = L" › ";

// Menu text marks the access key with '&', writes a literal '&' as "&&" and puts the shortcut
// key after a tab.
std::wstring ToDisplayText(const wchar_t* menuText)
{
    std::wstring text;
    for (const wchar_t* c = menuText; *c != L'\0' && *c != L'\t'; ++c)
    {
        if (*c == L'&')
        {
            if (c[1] != L'&')
                continue;
            ++c;
        }
        text.push_back(*c);
    }
    return text;
}

std::wstring ReadVerb(IContextMenu* handler, UINT commandOffset)
{
    wchar_t verb[128] = {};
    if (FAILED(handler->GetCommandString(commandOffset, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb),
                                         ARRAYSIZE(verb))))
    {
        return {};
    }
    return verb;
}

void ReadMenu(IContextMenu* handler, HMENU menu, std::vector<MenuEntry>& entries)
{
    const int count = GetMenuItemCount(menu);
    for (int position = 0; position < count; ++position)
    {
        wchar_t text[512] = {};
        MENUITEMINFOW item = {sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
        item.dwTypeData = text;
        item.cch = ARRAYSIZE(text);
        if (!GetMenuItemInfoW(menu, position, TRUE, &item))
            continue;

        MenuEntry entry;
        if ((item.fType & MFT_SEPARATOR) != 0)
        {
            entry.kind = MenuEntry::Kind::Separator;
        }
        else
        {
            entry.title = ToDisplayText(text);
            entry.enabled = (item.fState & MFS_DISABLED) == 0;
            if (item.wID >= kFirstCommandId)
                entry.verb = ReadVerb(handler, item.wID - kFirstCommandId);
            if (item.hSubMenu != nullptr)
            {
                entry.kind = MenuEntry::Kind::Submenu;
                ReadMenu(handler, item.hSubMenu, entry.children);
            }
            else if (item.wID >= kFirstCommandId)
            {
                entry.commandOffset = item.wID - kFirstCommandId;
            }
            else
            {
                continue;
            }
            if (entry.title.empty())
                continue;
        }
        entries.push_back(std::move(entry));
    }
}

const MenuEntry* FindCommand(const std::vector<MenuEntry>& entries, const MenuEntry& command)
{
    for (const MenuEntry& entry : entries)
    {
        if (entry.kind == MenuEntry::Kind::Command && entry.commandOffset == command.commandOffset &&
            entry.title == command.title)
        {
            return &entry;
        }
        if (entry.kind == MenuEntry::Kind::Submenu)
        {
            if (const MenuEntry* match = FindCommand(entry.children, command))
                return match;
        }
    }
    return nullptr;
}

bool IsSubmenu(const MenuEntry& entry, const wchar_t* verb)
{
    return entry.kind == MenuEntry::Kind::Submenu && entry.verb == verb;
}

// Adds an entry to a group whose items cannot open submenus: a submenu's items take its place,
// titled "Open archive › 7z".
void AppendLifted(const MenuEntry& entry, std::vector<MenuEntry>& entries)
{
    if (entry.kind != MenuEntry::Kind::Submenu)
    {
        entries.push_back(entry);
        return;
    }
    const std::wstring prefix = entry.DisplayTitle() + kSubmenuSeparator;
    for (MenuEntry child : entry.children)
    {
        if (child.kind != MenuEntry::Kind::Separator)
            child.titlePrefix = prefix;
        AppendLifted(child, entries);
    }
}

std::vector<MenuEntry> Lift(const std::vector<MenuEntry>& entries)
{
    std::vector<MenuEntry> lifted;
    for (const MenuEntry& entry : entries)
        AppendLifted(entry, lifted);
    return lifted;
}
}  // namespace

SevenZipMenu::~SevenZipMenu()
{
    if (menu_ != nullptr)
        DestroyMenu(menu_);
}

HRESULT SevenZipMenu::Load(IShellItemArray* selection)
{
    ComPtr<IDataObject> data;
    HRESULT hr = selection->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    if (FAILED(hr))
        return hr;

    ComPtr<IShellExtInit> init;
    hr = CoCreateInstance(kSevenZipShellExtension, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&init));
    if (FAILED(hr))
        return hr;
    // Explorer passes no folder for a selection; 7-Zip reads a folder as a drag-and-drop target.
    hr = init->Initialize(nullptr, data.Get(), nullptr);
    if (FAILED(hr))
        return hr;
    hr = init.As(&handler_);
    if (FAILED(hr))
        return hr;

    menu_ = CreatePopupMenu();
    if (menu_ == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());
    hr = handler_->QueryContextMenu(menu_, 0, kFirstCommandId, kLastCommandId, CMF_NORMAL);
    if (FAILED(hr))
        return hr;

    ReadMenu(handler_.Get(), menu_, entries_);
    return S_OK;
}

HRESULT SevenZipMenu::Invoke(UINT commandOffset, HWND owner) const
{
    if (!handler_)
        return E_UNEXPECTED;

    CMINVOKECOMMANDINFOEX info = {sizeof(info)};
    info.fMask = CMIC_MASK_UNICODE;
    info.hwnd = owner;
    info.lpVerb = MAKEINTRESOURCEA(commandOffset);
    info.lpVerbW = MAKEINTRESOURCEW(commandOffset);
    info.nShow = SW_SHOWNORMAL;
    return handler_->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
}

bool SelectGroup(const std::vector<MenuEntry>& topLevel, MenuSlot slot, MenuGroup& group)
{
    // 7-Zip's items: inside its "7-Zip" submenu (cascaded, the default) or directly in the menu.
    const auto cascadedMenu = std::find_if(topLevel.begin(), topLevel.end(), [](const MenuEntry& entry) {
        return IsSubmenu(entry, kCascadedMenuVerb);
    });
    std::vector<MenuEntry> items;
    if (cascadedMenu != topLevel.end())
        items = cascadedMenu->children;
    for (const MenuEntry& entry : topLevel)
    {
        if (cascadedMenu == topLevel.end() || &entry != &*cascadedMenu)
            items.push_back(entry);
    }

    // "CRC SHA" may sit inside the "7-Zip" submenu or next to it; either way it gets its own command.
    const auto checksumMenu = std::find_if(items.begin(), items.end(), [](const MenuEntry& entry) {
        return IsSubmenu(entry, kChecksumMenuVerb);
    });
    if (slot == MenuSlot::Hash)
    {
        if (checksumMenu == items.end())
            return false;
        group.title = checksumMenu->title;
        group.entries = Lift(checksumMenu->children);
        return !group.entries.empty();
    }

    if (checksumMenu != items.end())
        items.erase(checksumMenu);
    group.title = cascadedMenu != topLevel.end() ? cascadedMenu->title : kFlatMenuTitle;
    group.entries = Lift(items);
    return std::any_of(group.entries.begin(), group.entries.end(), [](const MenuEntry& entry) {
        return entry.kind != MenuEntry::Kind::Separator;
    });
}

HRESULT InvokeSevenZipCommand(IShellItemArray* selection, const MenuEntry& command, HWND owner)
{
    // 7-Zip keeps its command table per handler instance, so build the menu again for this call.
    SevenZipMenu menu;
    const HRESULT hr = menu.Load(selection);
    if (FAILED(hr))
        return hr;
    // The same selection and options give the same offsets; the title check catches an options
    // change in between.
    if (FindCommand(menu.Entries(), command) == nullptr)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    return menu.Invoke(command.commandOffset, owner);
}

std::wstring SevenZipIconLocation()
{
    wchar_t clsid[39] = {};
    StringFromGUID2(kSevenZipShellExtension, clsid, ARRAYSIZE(clsid));
    const std::wstring key = std::wstring(L"CLSID\\") + clsid + L"\\InprocServer32";

    wchar_t handlerPath[MAX_PATH] = {};
    DWORD size = sizeof(handlerPath);
    if (RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr,
                     handlerPath, &size) != ERROR_SUCCESS)
    {
        return {};
    }

    std::wstring location = handlerPath;
    const size_t folderEnd = location.find_last_of(L'\\');
    if (folderEnd == std::wstring::npos)
        return {};
    location.resize(folderEnd + 1);
    return location + L"7zFM.exe,0";
}
