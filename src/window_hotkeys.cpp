#include "window_hotkeys.h"

#include "main_window.h"

namespace
{
constexpr int kToggleId = 1;
constexpr UINT kModifiers = MOD_CONTROL | MOD_ALT | MOD_NOREPEAT;
}

// НАЧАЛО: глобальные сочетания клавиш для управления окном.
bool WindowHotkeys::Register(HWND window)
{
    // Ctrl+Alt+M — скрыть в трей или восстановить окно тем же сочетанием.
    // MOD_NOREPEAT предотвращает повторные события при удержании клавиш.
    return RegisterHotKey(window, kToggleId, kModifiers, 'M') != FALSE;
}

void WindowHotkeys::Unregister(HWND window)
{
    // При закрытии приложения освобождаем сочетания для других программ.
    UnregisterHotKey(window, kToggleId);
}

bool WindowHotkeys::Handle(HWND window, WPARAM hotkey_id)
{
    if (hotkey_id == kToggleId)
    {
        MainWindow::ToggleVisibility(window);
        return true;
    }
    return false;
}
// КОНЕЦ: глобальные сочетания клавиш для управления окном.
