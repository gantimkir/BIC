#pragma once

#include <windows.h>

namespace WindowHotkeys
{
bool Register(HWND window);
void Unregister(HWND window);
bool Handle(HWND window, WPARAM hotkey_id);
}
