#include "tray_icon.h"

#include <shellapi.h>

#include "resource.h"
#include "main_window.h"

namespace
{
constexpr UINT kIconId = 1;
constexpr UINT kOpenCommand = 1;
constexpr UINT kExitCommand = 2;

}

// НАЧАЛО: значок приложения в системном трее.
bool TrayIcon::Add(HWND window, HINSTANCE instance)
{
    NOTIFYICONDATAW icon_data{};
    icon_data.cbSize = sizeof(icon_data);
    icon_data.hWnd = window;
    icon_data.uID = kIconId;
    icon_data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    icon_data.uCallbackMessage = kCallbackMessage;
    icon_data.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_BIC),
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    lstrcpyW(icon_data.szTip, L"BIC — Business information center");
    return icon_data.hIcon != nullptr && Shell_NotifyIconW(NIM_ADD, &icon_data) != FALSE;
}

void TrayIcon::Remove(HWND window)
{
    NOTIFYICONDATAW icon_data{};
    icon_data.cbSize = sizeof(icon_data);
    icon_data.hWnd = window;
    icon_data.uID = kIconId;
    Shell_NotifyIconW(NIM_DELETE, &icon_data);
}

void TrayIcon::HandleEvent(HWND window, LPARAM event)
{
    if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK)
    {
        MainWindow::Show(window);
    }
    else if (event == WM_RBUTTONUP)
    {
        HMENU menu = CreatePopupMenu();
        if (menu == nullptr)
        {
            return;
        }
        AppendMenuW(menu, MF_STRING, kOpenCommand, L"Открыть");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kExitCommand, L"Выход");

        POINT position{};
        GetCursorPos(&position);
        SetForegroundWindow(window);
        const UINT command = static_cast<UINT>(TrackPopupMenu(menu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            position.x, position.y, 0, window, nullptr));
        DestroyMenu(menu);
        // Позволяет меню трея корректно закрываться при щелчке вне него.
        PostMessageW(window, WM_NULL, 0, 0);

        if (command == kOpenCommand)
        {
            MainWindow::Show(window);
        }
        else if (command == kExitCommand)
        {
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
    }
}
// КОНЕЦ: значок приложения в системном трее.
