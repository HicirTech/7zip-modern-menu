#pragma once

#include "SevenZipMenu.h"

// Creates the top-level command of a slot; Explorer gets it through DllGetClassObject.
HRESULT CreateRootCommand(MenuSlot slot, REFIID riid, void** object);
