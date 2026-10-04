#pragma once

#include <windows.h>

namespace TrayIcon
{
constexpr UINT kCallbackMessage = WM_APP + 1;

bool Add(HWND window, HINSTANCE instance);
void Remove(HWND window);
void HandleEvent(HWND window, LPARAM event);
}
