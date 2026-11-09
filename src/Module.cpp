#include "CommandIds.h"
#include "ComObject.h"
#include "ExplorerCommands.h"

#include <shlwapi.h>

namespace
{
LONG g_moduleReferences = 0;

class ClassFactory final : public ComObject, public IClassFactory
{
public:
    explicit ClassFactory(MenuSlot slot) noexcept : slot_(slot) {}

    IFACEMETHODIMP QueryInterface(REFIID riid, void** object) override
    {
        static const QITAB interfaces[] = {
            QITABENT(ClassFactory, IClassFactory),
            {},
        };
        return QISearch(this, interfaces, riid, object);
    }

    IFACEMETHODIMP_(ULONG) AddRef() override { return AddRefImpl(); }
    IFACEMETHODIMP_(ULONG) Release() override { return ReleaseImpl(); }

    IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override
    {
        *object = nullptr;
        if (outer != nullptr)
            return CLASS_E_NOAGGREGATION;
        return CreateRootCommand(slot_, riid, object);
    }

    IFACEMETHODIMP LockServer(BOOL lock) override
    {
        if (lock)
            ModuleAddRef();
        else
            ModuleRelease();
        return S_OK;
    }

private:
    const MenuSlot slot_;
};
}  // namespace

void ModuleAddRef() noexcept
{
    InterlockedIncrement(&g_moduleReferences);
}

void ModuleRelease() noexcept
{
    InterlockedDecrement(&g_moduleReferences);
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void** object)
{
    *object = nullptr;
    MenuSlot slot;
    if (IsEqualCLSID(clsid, kMainCommandClsid))
        slot = MenuSlot::Main;
    else if (IsEqualCLSID(clsid, kHashCommandClsid))
        slot = MenuSlot::Hash;
    else
        return CLASS_E_CLASSNOTAVAILABLE;

    auto* factory = new (std::nothrow) ClassFactory(slot);
    if (factory == nullptr)
        return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, object);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return g_moduleReferences == 0 ? S_OK : S_FALSE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}
