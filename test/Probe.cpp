// Runs the 7-Zip commands outside Explorer:
//   probe.exe [--dll <path>] [--invoke <text>] [--legacy] <path>...
// Prints what the "7-Zip" and "CRC SHA" commands show for the selection. With --dll the commands
// come from that DLL, otherwise from their registration (the installed package). --invoke runs
// the first command whose title contains <text>. --legacy also prints the shell's classic menu.

#include "../src/CommandIds.h"

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
struct ComError
{
    HRESULT result;
    const wchar_t* operation;
};

void Check(HRESULT result, const wchar_t* operation)
{
    if (FAILED(result))
        throw ComError{result, operation};
}

struct Options
{
    std::wstring dllPath;
    std::wstring invokeText;
    bool legacy = false;
    std::vector<std::wstring> paths;
};

bool ParseArguments(int argc, wchar_t** argv, Options& options)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring argument = argv[i];
        if (argument == L"--dll" && i + 1 < argc)
            options.dllPath = argv[++i];
        else if (argument == L"--invoke" && i + 1 < argc)
            options.invokeText = argv[++i];
        else if (argument == L"--legacy")
            options.legacy = true;
        else if (argument.rfind(L"--", 0) == 0)
            return false;
        else
            options.paths.push_back(argument);
    }
    return !options.paths.empty();
}

class IdLists
{
public:
    explicit IdLists(const std::vector<std::wstring>& paths)
    {
        for (const std::wstring& path : paths)
        {
            PIDLIST_ABSOLUTE item = nullptr;
            Check(SHParseDisplayName(path.c_str(), nullptr, &item, 0, nullptr), L"SHParseDisplayName");
            items_.push_back(item);
        }
    }

    IdLists(const IdLists&) = delete;
    IdLists& operator=(const IdLists&) = delete;

    ~IdLists()
    {
        for (PIDLIST_ABSOLUTE item : items_)
            CoTaskMemFree(item);
    }

    const std::vector<PIDLIST_ABSOLUTE>& Items() const { return items_; }

private:
    std::vector<PIDLIST_ABSOLUTE> items_;
};

ComPtr<IShellItemArray> CreateSelection(const IdLists& idLists)
{
    std::vector<PCIDLIST_ABSOLUTE> items(idLists.Items().begin(), idLists.Items().end());
    ComPtr<IShellItemArray> selection;
    Check(SHCreateShellItemArrayFromIDLists(static_cast<UINT>(items.size()), items.data(), &selection),
          L"SHCreateShellItemArrayFromIDLists");
    return selection;
}

using GetClassObjectFunction = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);

ComPtr<IExplorerCommand> CreateCommand(const CLSID& clsid, HMODULE dll)
{
    ComPtr<IExplorerCommand> command;
    if (dll == nullptr)
    {
        Check(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&command)), L"CoCreateInstance");
        return command;
    }

    const auto getClassObject = reinterpret_cast<GetClassObjectFunction>(GetProcAddress(dll, "DllGetClassObject"));
    if (getClassObject == nullptr)
        Check(HRESULT_FROM_WIN32(GetLastError()), L"GetProcAddress");
    ComPtr<IClassFactory> factory;
    Check(getClassObject(clsid, IID_PPV_ARGS(&factory)), L"DllGetClassObject");
    Check(factory->CreateInstance(nullptr, IID_PPV_ARGS(&command)), L"IClassFactory::CreateInstance");
    return command;
}

std::wstring TakeString(LPWSTR value)
{
    std::wstring text = value != nullptr ? value : L"";
    CoTaskMemFree(value);
    return text;
}

// Returns how many submenus sit below a top-level command; Explorer does not open those.
int PrintCommand(IExplorerCommand* command, IShellItemArray* selection, int depth)
{
    const std::wstring indent(static_cast<size_t>(depth) * 2, L' ');
    EXPCMDFLAGS flags = 0;
    Check(command->GetFlags(&flags), L"GetFlags");
    if ((flags & ECF_ISSEPARATOR) != 0)
    {
        wprintf(L"%ls--------\n", indent.c_str());
        return 0;
    }

    EXPCMDSTATE state = ECS_ENABLED;
    Check(command->GetState(selection, TRUE, &state), L"GetState");
    LPWSTR title = nullptr;
    const HRESULT titleResult = command->GetTitle(selection, &title);
    LPWSTR icon = nullptr;
    command->GetIcon(selection, &icon);
    const std::wstring titleText = SUCCEEDED(titleResult) ? TakeString(title) : L"<no title>";
    const std::wstring iconText = TakeString(icon);
    wprintf(L"%ls%ls%ls%ls%ls%ls\n", indent.c_str(), titleText.c_str(), (state & ECS_HIDDEN) ? L"  [hidden]" : L"",
            (state & ECS_DISABLED) ? L"  [disabled]" : L"", iconText.empty() ? L"" : L"  icon=", iconText.c_str());

    if ((flags & ECF_HASSUBCOMMANDS) == 0 || (state & ECS_HIDDEN) != 0)
        return 0;
    int nestedSubmenus = 0;
    if (depth > 0)
    {
        wprintf(L"%ls^ submenu below a top-level command: Explorer does not open it\n", indent.c_str());
        ++nestedSubmenus;
    }
    ComPtr<IEnumExplorerCommand> children;
    Check(command->EnumSubCommands(&children), L"EnumSubCommands");
    ComPtr<IExplorerCommand> child;
    while (children->Next(1, child.ReleaseAndGetAddressOf(), nullptr) == S_OK)
        nestedSubmenus += PrintCommand(child.Get(), selection, depth + 1);
    return nestedSubmenus;
}

ComPtr<IExplorerCommand> FindCommand(IExplorerCommand* command, IShellItemArray* selection, const std::wstring& text)
{
    EXPCMDFLAGS flags = 0;
    Check(command->GetFlags(&flags), L"GetFlags");
    if ((flags & ECF_ISSEPARATOR) != 0)
        return nullptr;
    EXPCMDSTATE state = ECS_ENABLED;
    Check(command->GetState(selection, TRUE, &state), L"GetState");
    if ((state & ECS_HIDDEN) != 0)
        return nullptr;

    if ((flags & ECF_HASSUBCOMMANDS) == 0)
    {
        LPWSTR title = nullptr;
        if (SUCCEEDED(command->GetTitle(selection, &title)) && TakeString(title).find(text) != std::wstring::npos)
            return command;
        return nullptr;
    }

    ComPtr<IEnumExplorerCommand> children;
    Check(command->EnumSubCommands(&children), L"EnumSubCommands");
    ComPtr<IExplorerCommand> child;
    while (children->Next(1, child.ReleaseAndGetAddressOf(), nullptr) == S_OK)
    {
        if (ComPtr<IExplorerCommand> match = FindCommand(child.Get(), selection, text))
            return match;
    }
    return nullptr;
}

// Prints the menu; submenus also show their verb, which is how the bridge recognises 7-Zip's.
void PrintMenu(IContextMenu* handler, HMENU menu, int depth)
{
    const std::wstring indent(static_cast<size_t>(depth) * 2, L' ');
    const int count = GetMenuItemCount(menu);
    for (int position = 0; position < count; ++position)
    {
        wchar_t text[512] = {};
        MENUITEMINFOW item = {sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_SUBMENU | MIIM_STRING | MIIM_ID;
        item.dwTypeData = text;
        item.cch = ARRAYSIZE(text);
        if (!GetMenuItemInfoW(menu, position, TRUE, &item))
            continue;
        if ((item.fType & MFT_SEPARATOR) != 0)
        {
            wprintf(L"%ls--------\n", indent.c_str());
            continue;
        }
        wchar_t verb[128] = {};
        if (item.hSubMenu != nullptr && item.wID >= 1)
        {
            handler->GetCommandString(item.wID - 1, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb),
                                      ARRAYSIZE(verb));
        }
        wprintf(L"%ls%ls%ls%ls\n", indent.c_str(), text, verb[0] != L'\0' ? L"  verb=" : L"", verb);
        if (item.hSubMenu != nullptr)
            PrintMenu(handler, item.hSubMenu, depth + 1);
    }
}

// The menu Explorer shows under "Show more options": all items share the first item's folder.
void PrintClassicMenu(const IdLists& idLists)
{
    ComPtr<IShellFolder> parent;
    PCUITEMID_CHILD firstChild = nullptr;
    Check(SHBindToParent(idLists.Items().front(), IID_PPV_ARGS(&parent), &firstChild), L"SHBindToParent");
    std::vector<PCUITEMID_CHILD> children;
    for (PIDLIST_ABSOLUTE item : idLists.Items())
        children.push_back(ILFindLastID(item));

    ComPtr<IContextMenu> contextMenu;
    Check(parent->GetUIObjectOf(nullptr, static_cast<UINT>(children.size()), children.data(), IID_IContextMenu,
                                nullptr, reinterpret_cast<void**>(contextMenu.ReleaseAndGetAddressOf())),
          L"GetUIObjectOf");
    HMENU menu = CreatePopupMenu();
    const HRESULT result = contextMenu->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
    if (SUCCEEDED(result))
        PrintMenu(contextMenu.Get(), menu, 0);
    DestroyMenu(menu);
    Check(result, L"QueryContextMenu");
}

int Run(const Options& options)
{
    HMODULE dll = nullptr;
    if (!options.dllPath.empty())
    {
        dll = LoadLibraryW(options.dllPath.c_str());
        if (dll == nullptr)
            Check(HRESULT_FROM_WIN32(GetLastError()), L"LoadLibrary");
    }

    const IdLists idLists(options.paths);
    const ComPtr<IShellItemArray> selection = CreateSelection(idLists);
    const CLSID commands[] = {kMainCommandClsid, kHashCommandClsid};
    int nestedSubmenus = 0;
    for (const CLSID& clsid : commands)
    {
        // Packaged in-process classes are not registered for every process: report and go on
        // to the classic menu, which shows whether the shell itself finds them.
        try
        {
            nestedSubmenus += PrintCommand(CreateCommand(clsid, dll).Get(), selection.Get(), 0);
        }
        catch (const ComError& error)
        {
            fwprintf(stderr, L"%ls failed: 0x%08lX\n", error.operation, static_cast<unsigned long>(error.result));
            if (!options.legacy)
                throw;
        }
    }

    if (!options.invokeText.empty())
    {
        ComPtr<IExplorerCommand> target;
        for (const CLSID& clsid : commands)
        {
            if (!target)
                target = FindCommand(CreateCommand(clsid, dll).Get(), selection.Get(), options.invokeText);
        }
        if (!target)
        {
            fwprintf(stderr, L"No command title contains \"%ls\".\n", options.invokeText.c_str());
            return 3;
        }
        Check(target->Invoke(selection.Get(), nullptr), L"Invoke");
        wprintf(L"Invoked.\n");
    }

    if (options.legacy)
    {
        wprintf(L"--- classic menu\n");
        PrintClassicMenu(idLists);
    }

    if (nestedSubmenus > 0)
    {
        fwprintf(stderr, L"%d submenu(s) below a top-level command.\n", nestedSubmenus);
        return 4;
    }
    return 0;
}
}  // namespace

int wmain(int argc, wchar_t** argv)
{
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);

    Options options;
    if (!ParseArguments(argc, argv, options))
    {
        fwprintf(stderr, L"usage: probe.exe [--dll <path>] [--invoke <text>] [--legacy] <path>...\n");
        return 2;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
        return 1;

    int exitCode = 1;
    try
    {
        exitCode = Run(options);
    }
    catch (const ComError& error)
    {
        fwprintf(stderr, L"%ls failed: 0x%08lX\n", error.operation, static_cast<unsigned long>(error.result));
    }
    CoUninitialize();
    return exitCode;
}
