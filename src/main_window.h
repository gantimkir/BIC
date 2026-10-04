#pragma once

#include <windows.h>

namespace MainWindow
{
inline constexpr wchar_t kClassName[] = L"BIC.MainWindow";
inline constexpr wchar_t kTitle[] = L"Business information center";

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
void Show(HWND window);
void HideToTray(HWND window);
void ToggleVisibility(HWND window);
}
