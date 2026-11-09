#pragma once

#include <windows.h>

#include <new>

// Counts live COM objects and server locks; DllCanUnloadNow allows unloading at zero.
void ModuleAddRef() noexcept;
void ModuleRelease() noexcept;

// Reference counting shared by the COM objects of this DLL. An object starts with the one
// reference its creator owns.
class ComObject
{
public:
    ComObject(const ComObject&) = delete;
    ComObject& operator=(const ComObject&) = delete;

protected:
    ComObject() noexcept { ModuleAddRef(); }
    virtual ~ComObject() { ModuleRelease(); }

    ULONG AddRefImpl() noexcept { return static_cast<ULONG>(InterlockedIncrement(&references_)); }

    ULONG ReleaseImpl() noexcept
    {
        const LONG references = InterlockedDecrement(&references_);
        if (references == 0)
            delete this;
        return static_cast<ULONG>(references);
    }

private:
    LONG references_ = 1;
};

// The DLL runs inside Explorer: no C++ exception may leave a COM method.
template <typename Function>
HRESULT CatchAll(Function&& function) noexcept
{
    try
    {
        return function();
    }
    catch (const std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    catch (...)
    {
        return E_UNEXPECTED;
    }
}
