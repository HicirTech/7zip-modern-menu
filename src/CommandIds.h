#pragma once

#include <windows.h>

// CLSIDs of the two top-level commands; package\AppxManifest.xml registers the same values.

// {7B6E4117-EFEB-47AA-9E27-2DFFD7F17E92}: "7-Zip", the submenu with 7-Zip's commands.
inline constexpr CLSID kMainCommandClsid =
    {0x7b6e4117, 0xefeb, 0x47aa, {0x9e, 0x27, 0x2d, 0xff, 0xd7, 0xf1, 0x7e, 0x92}};

// {E0D7DE7F-98E2-4129-9FB7-A334CF2DBD8B}: "CRC SHA", the submenu with 7-Zip's hash commands.
inline constexpr CLSID kHashCommandClsid =
    {0xe0d7de7f, 0x98e2, 0x4129, {0x9f, 0xb7, 0xa3, 0x34, 0xcf, 0x2d, 0xbd, 0x8b}};
