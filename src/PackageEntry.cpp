#include <windows.h>

// A package needs an application with an executable. Only the context menu handlers of this
// package are used, and AppListEntry="none" keeps this entry out of Start, so it does nothing.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return 0;
}
