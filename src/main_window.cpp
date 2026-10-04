#include "main_window.h"

#include "tray_icon.h"
#include "window_hotkeys.h"

namespace
{
constexpr int kCloseButtonId = 1;
constexpr int kButtonWidth = 100;
constexpr int kButtonHeight = 32;
constexpr int kButtonMargin = 16;

LRESULT OnCreate(HWND window, HINSTANCE instance)
{
    HWND button = CreateWindowExW(
        0, L"BUTTON", L"Закрыть",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, kButtonWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseButtonId)),
        instance, nullptr);
    if (button == nullptr)
    {
        return -1;
    }
    SendMessageW(button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    // НАЧАЛО: добавление иконки приложения в трей.
    if (!TrayIcon::Add(window, instance))
    {
        MessageBoxW(window, L"Не удалось добавить значок приложения в трей.",
            MainWindow::kTitle, MB_OK | MB_ICONWARNING);
    }
    // КОНЕЦ: добавление иконки приложения в трей.
    // НАЧАЛО: регистрация глобальных сочетаний клавиш.
    if (!WindowHotkeys::Register(window))
    {
        MessageBoxW(window,
            L"Не удалось зарегистрировать Ctrl+Alt+M.\n"
            L"Сочетание может быть занято другой программой.\n"
            L"Управление окном через кнопки и трей остаётся доступным.",
            MainWindow::kTitle, MB_OK | MB_ICONWARNING);
    }
    // КОНЕЦ: регистрация глобальных сочетаний клавиш.
    return 0;
}

void OnResize(HWND window)
{
    RECT client{};
    GetClientRect(window, &client);
    MoveWindow(GetDlgItem(window, kCloseButtonId),
        client.right > kButtonWidth + kButtonMargin ? client.right - kButtonWidth - kButtonMargin : 0,
        client.bottom > kButtonHeight + kButtonMargin ? client.bottom - kButtonHeight - kButtonMargin : 0,
        kButtonWidth, kButtonHeight, TRUE);
}

bool OnCommand(HWND window, WPARAM w_param, LPARAM l_param)
{
    if (LOWORD(w_param) == kCloseButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kCloseButtonId))
    {
        SendMessageW(window, WM_CLOSE, 0, 0);
        return true;
    }
    return false;
}

void OnDestroy(HWND window)
{
    WindowHotkeys::Unregister(window);
    TrayIcon::Remove(window);
    PostQuitMessage(0);
}
} // namespace

// НАЧАЛО: сворачивание в трей и восстановление окна.
void MainWindow::Show(HWND window)
{
    // Восстанавливаем свёрнутое окно; скрытое сохраняет прежний размер.
    ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window);
}

void MainWindow::HideToTray(HWND window)
{
    // Скрытое окно исчезает с панели задач и из Alt+Tab, процесс продолжает работу.
    ShowWindow(window, SW_HIDE);
}

void MainWindow::ToggleVisibility(HWND window)
{
    if (!IsWindowVisible(window) || IsIconic(window))
    {
        Show(window);
    }
    else
    {
        HideToTray(window);
    }
}
// КОНЕЦ: сворачивание в трей и восстановление окна.

LRESULT CALLBACK MainWindow::WindowProcedure(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    // Восстанавливаем значок трея после перезапуска Проводника Windows.
    static const UINT taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbar_created != 0 && message == taskbar_created)
    {
        TrayIcon::Add(window, reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)));
        return 0;
    }

    switch (message)
    {
    case WM_CREATE:
        return OnCreate(window, reinterpret_cast<LPCREATESTRUCTW>(l_param)->hInstance);
    case WM_SIZE:
        // Обычная кнопка сворачивания тоже убирает окно в трей.
        if (w_param == SIZE_MINIMIZED)
        {
            HideToTray(window);
        }
        OnResize(window);
        return 0;
    case WM_COMMAND:
        if (OnCommand(window, w_param, l_param))
        {
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (WindowHotkeys::Handle(window, w_param))
        {
            return 0;
        }
        break;
    case TrayIcon::kCallbackMessage:
        TrayIcon::HandleEvent(window, l_param);
        return 0;
    case WM_DESTROY:
        OnDestroy(window);
        return 0;
    }

    // Необработанные сообщения передаём стандартному обработчику Windows.
    return DefWindowProcW(window, message, w_param, l_param);
}
