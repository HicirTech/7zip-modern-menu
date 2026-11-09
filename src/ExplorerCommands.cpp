#include "ExplorerCommands.h"

#include "CommandIds.h"
#include "ComObject.h"

#include <shlwapi.h>

#include <algorithm>
#include <mutex>
#include <optional>

using Microsoft::WRL::ComPtr;

namespace
{
// The site is where the owner window for 7-Zip's dialogs comes from.
class ObjectWithSite : public IObjectWithSite
{
public:
    IFACEMETHODIMP SetSite(IUnknown* site) override
    {
        site_ = site;
        return S_OK;
    }

    IFACEMETHODIMP GetSite(REFIID riid, void** site) override
    {
        *site = nullptr;
        return site_ ? site_->QueryInterface(riid, site) : E_FAIL;
    }

protected:
    HWND OwnerWindow() const
    {
        HWND window = nullptr;
        if (site_)
            IUnknown_GetWindow(site_.Get(), &window);
        return window;
    }

private:
    ComPtr<IUnknown> site_;
};

// An item of a top-level command's submenu: a 7-Zip command or a separator. Explorer opens no
// submenus at this level, so SelectGroup has already lifted their items up here.
class SubCommand final : public ComObject, public IExplorerCommand, public ObjectWithSite
{
public:
    explicit SubCommand(MenuEntry entry) : entry_(std::move(entry)) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        static const QITAB interfaces[] = {
            QITABENT(SubCommand, IExplorerCommand),
            QITABENT(SubCommand, IObjectWithSite),
            {},
        };
        return QISearch(this, interfaces, riid, object);
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return AddRefImpl(); }
    IFACEMETHODIMP_(ULONG) Release() override { return ReleaseImpl(); }

    IFACEMETHODIMP GetTitle(IShellItemArray*, LPWSTR* name) override
    {
        *name = nullptr;
        if (entry_.kind == MenuEntry::Kind::Separator)
            return E_NOTIMPL;
        return CatchAll([&] { return SHStrDupW(entry_.DisplayTitle().c_str(), name); });
    }

    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override
    {
        *icon = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* infoTip) override
    {
        *infoTip = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(GUID* commandName) override
    {
        *commandName = GUID_NULL;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* state) override
    {
        *state = entry_.enabled ? ECS_ENABLED : ECS_DISABLED;
        return S_OK;
    }

    IFACEMETHODIMP Invoke(IShellItemArray* selection, IBindCtx*) override
    {
        if (entry_.kind != MenuEntry::Kind::Command)
            return E_NOTIMPL;
        if (selection == nullptr)
            return E_INVALIDARG;
        return CatchAll([&] { return InvokeSevenZipCommand(selection, entry_, OwnerWindow()); });
    }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override
    {
        *flags = entry_.kind == MenuEntry::Kind::Separator ? ECF_ISSEPARATOR : ECF_DEFAULT;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override
    {
        *commands = nullptr;
        return E_NOTIMPL;
    }

private:
    const MenuEntry entry_;
};

class CommandEnum final : public ComObject, public IEnumExplorerCommand
{
public:
    explicit CommandEnum(std::vector<ComPtr<IExplorerCommand>> commands, size_t position = 0)
        : commands_(std::move(commands)), position_(position)
    {
    }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        static const QITAB interfaces[] = {
            QITABENT(CommandEnum, IEnumExplorerCommand),
            {},
        };
        return QISearch(this, interfaces, riid, object);
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return AddRefImpl(); }
    IFACEMETHODIMP_(ULONG) Release() override { return ReleaseImpl(); }

    IFACEMETHODIMP Next(ULONG count, IExplorerCommand** commands, ULONG* fetched) override
    {
        ULONG copied = 0;
        while (copied < count && position_ < commands_.size())
        {
            commands_[position_].CopyTo(&commands[copied]);
            ++position_;
            ++copied;
        }
        if (fetched != nullptr)
            *fetched = copied;
        return copied == count ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Skip(ULONG count) override
    {
        const size_t skipped = std::min<size_t>(count, commands_.size() - position_);
        position_ += skipped;
        return skipped == count ? S_OK : S_FALSE;
    }

    IFACEMETHODIMP Reset() override
    {
        position_ = 0;
        return S_OK;
    }

    IFACEMETHODIMP Clone(IEnumExplorerCommand** copy) override
    {
        *copy = nullptr;
        return CatchAll([&] {
            *copy = new CommandEnum(commands_, position_);
            return S_OK;
        });
    }

private:
    const std::vector<ComPtr<IExplorerCommand>> commands_;
    size_t position_;
};

ComPtr<IExplorerCommand> MakeSubCommand(MenuEntry entry)
{
    ComPtr<IExplorerCommand> command;
    command.Attach(new SubCommand(std::move(entry)));
    return command;
}

HRESULT CreateCommandEnum(const std::vector<MenuEntry>& entries, IEnumExplorerCommand** commands)
{
    std::vector<ComPtr<IExplorerCommand>> items;
    bool separatorPending = false;
    for (const MenuEntry& entry : entries)
    {
        // Like a menu, drop leading, trailing and doubled separators.
        if (entry.kind == MenuEntry::Kind::Separator)
        {
            separatorPending = !items.empty();
            continue;
        }
        if (separatorPending)
        {
            MenuEntry separator;
            separator.kind = MenuEntry::Kind::Separator;
            items.push_back(MakeSubCommand(std::move(separator)));
            separatorPending = false;
        }
        items.push_back(MakeSubCommand(entry));
    }
    *commands = new CommandEnum(std::move(items));
    return S_OK;
}

// A top-level entry of the new menu ("7-Zip" or "CRC SHA"); its submenu is 7-Zip's own menu.
class RootCommand final : public ComObject, public IExplorerCommand, public ObjectWithSite
{
public:
    explicit RootCommand(MenuSlot slot) noexcept : slot_(slot) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        static const QITAB interfaces[] = {
            QITABENT(RootCommand, IExplorerCommand),
            QITABENT(RootCommand, IObjectWithSite),
            {},
        };
        return QISearch(this, interfaces, riid, object);
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return AddRefImpl(); }
    IFACEMETHODIMP_(ULONG) Release() override { return ReleaseImpl(); }

    IFACEMETHODIMP GetTitle(IShellItemArray* selection, LPWSTR* name) override
    {
        *name = nullptr;
        return CatchAll([&] {
            const std::optional<MenuGroup> group = CurrentGroup(selection);
            return group ? SHStrDupW(group->title.c_str(), name) : E_FAIL;
        });
    }

    IFACEMETHODIMP GetIcon(IShellItemArray*, LPWSTR* icon) override
    {
        *icon = nullptr;
        if (slot_ != MenuSlot::Main)
            return E_NOTIMPL;
        return CatchAll([&] {
            const std::wstring location = SevenZipIconLocation();
            return location.empty() ? E_FAIL : SHStrDupW(location.c_str(), icon);
        });
    }

    IFACEMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* infoTip) override
    {
        *infoTip = nullptr;
        return E_NOTIMPL;
    }

    IFACEMETHODIMP GetCanonicalName(GUID* commandName) override
    {
        *commandName = slot_ == MenuSlot::Main ? kMainCommandClsid : kHashCommandClsid;
        return S_OK;
    }

    IFACEMETHODIMP GetState(IShellItemArray* selection, BOOL, EXPCMDSTATE* state) override
    {
        *state = ECS_HIDDEN;
        return CatchAll([&] {
            if (selection != nullptr && Refresh(selection))
                *state = ECS_ENABLED;
            return S_OK;
        });
    }

    IFACEMETHODIMP Invoke(IShellItemArray*, IBindCtx*) override { return E_NOTIMPL; }

    IFACEMETHODIMP GetFlags(EXPCMDFLAGS* flags) override
    {
        *flags = ECF_HASSUBCOMMANDS;
        return S_OK;
    }

    IFACEMETHODIMP EnumSubCommands(IEnumExplorerCommand** commands) override
    {
        *commands = nullptr;
        return CatchAll([&] {
            const std::optional<MenuGroup> group = CurrentGroup(nullptr);
            return group ? CreateCommandEnum(group->entries, commands) : E_UNEXPECTED;
        });
    }

private:
    // Reads 7-Zip's menu for the selection; false when 7-Zip shows nothing for this slot.
    bool Refresh(IShellItemArray* selection)
    {
        std::optional<MenuGroup> group;
        SevenZipMenu menu;
        if (SUCCEEDED(menu.Load(selection)))
        {
            MenuGroup found;
            if (SelectGroup(menu.Entries(), slot_, found))
                group = std::move(found);
        }
        const bool shown = group.has_value();
        const std::lock_guard lock(mutex_);
        group_ = std::move(group);
        return shown;
    }

    // EnumSubCommands gets no selection, so it uses what GetState read; GetTitle may come first.
    std::optional<MenuGroup> CurrentGroup(IShellItemArray* selection)
    {
        {
            const std::lock_guard lock(mutex_);
            if (group_)
                return group_;
        }
        if (selection != nullptr && Refresh(selection))
            return CurrentGroup(nullptr);
        return std::nullopt;
    }

    const MenuSlot slot_;
    std::mutex mutex_;
    std::optional<MenuGroup> group_;
};
}  // namespace

HRESULT CreateRootCommand(MenuSlot slot, REFIID riid, void** object)
{
    *object = nullptr;
    auto* command = new (std::nothrow) RootCommand(slot);
    if (command == nullptr)
        return E_OUTOFMEMORY;
    const HRESULT hr = command->QueryInterface(riid, object);
    command->Release();
    return hr;
}
