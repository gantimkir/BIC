#include "main_window.h"

#include "tray_icon.h"
#include "window_hotkeys.h"
#include "contract_registry.h"
#include "ip_letterhead.h"
#include "ki_letterhead.h"
#include "pzz_61_48_text.h"
#include "pzz_61_41_text.h"
#include "pzz_61_48_graphics.h"
#include "pzz_61_41_graphics.h"

namespace
{
constexpr int kCloseButtonId = 1;
constexpr int kContractRegistryButtonId = 2;
constexpr int kIpLetterheadButtonId = 3;
constexpr int kKiLetterheadButtonId = 4;
constexpr int kPzz6148TextButtonId = 5;
constexpr int kPzz6141TextButtonId = 6;
constexpr int kPzz6148GraphicsButtonId = 7;
constexpr int kPzz6141GraphicsButtonId = 8;
constexpr int kPzz6148GroupId = 9;
constexpr int kPzz6141GroupId = 10;
constexpr int kButtonWidth = 100;
constexpr int kRegistryButtonWidth = 180;
constexpr int kButtonHeight = 32;
constexpr int kButtonMargin = 16;
constexpr int kButtonSpacing = 8;
// Общие размеры обеих рамок: отступы по бокам и зазор между кнопками равны.
// Надпись рамки занимает верхние 13 пикселей, нижняя граница — ещё 3 пикселя.
constexpr int kPzzGroupSideInset = 4;
constexpr int kPzzGroupButtonGap = kPzzGroupSideInset;
constexpr int kPzzGroupTopInset = 13;
constexpr int kPzzGroupBottomInset = 3;
constexpr int kPzzGroupHeight = kPzzGroupTopInset + kButtonHeight + kPzzGroupBottomInset;
constexpr int kPzzGroupButtonWidth =
    (kRegistryButtonWidth - 2 * kPzzGroupSideInset - kPzzGroupButtonGap) / 2;
bool tray_icon_available = false;

// Создаёт рамку и две команды ПЗЗ с одинаковой геометрией (180 x 48, кнопки 84 x 32).
// Возвращает false при ошибке создания любого элемента; OnCreate тогда возвращает -1,
// и Windows уничтожает главное окно вместе с уже созданными дочерними элементами.
// Рамка только рисует границу: кнопки остаются дочерними элементами главного окна,
// поэтому прежние WM_COMMAND, GetDlgItem и ID команд продолжают работать.
bool CreatePzzGroup(HWND window, HINSTANCE instance, const wchar_t* caption,
    int group_id, int text_button_id, int graphics_button_id, int top)
{
    HWND group = CreateWindowExW(
        0, L"BUTTON", caption,
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_GROUPBOX,
        kButtonMargin, top, kRegistryButtonWidth, kPzzGroupHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(group_id)), instance, nullptr);
    if (group == nullptr)
    {
        return false;
    }
    // WS_CLIPSIBLINGS не позволяет рамке закрашивать соседние кнопки при перерисовке.
    SendMessageW(group, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    HWND text_button = CreateWindowExW(
        0, L"BUTTON", L"Текст",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin + kPzzGroupSideInset, top + kPzzGroupTopInset,
        kPzzGroupButtonWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(text_button_id)), instance, nullptr);
    if (text_button == nullptr)
    {
        return false;
    }
    SendMessageW(text_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Правая кнопка отделена от левой тем же зазором, что и от боковых границ рамки.
    HWND graphics_button = CreateWindowExW(
        0, L"BUTTON", L"Графика",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin + kPzzGroupSideInset + kPzzGroupButtonWidth + kPzzGroupButtonGap,
        top + kPzzGroupTopInset, kPzzGroupButtonWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(graphics_button_id)), instance, nullptr);
    if (graphics_button == nullptr)
    {
        return false;
    }
    SendMessageW(graphics_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return true;
}

LRESULT OnCreate(HWND window, HINSTANCE instance)
{
    HWND registry_button = CreateWindowExW(
        0, L"BUTTON", L"Реестр договоров",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin, kButtonMargin, kRegistryButtonWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kContractRegistryButtonId)),
        instance, nullptr);
    if (registry_button == nullptr)
    {
        return -1;
    }
    SendMessageW(registry_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Бланк располагается под реестром; команды кнопок обрабатываются отдельно в WM_COMMAND.
    HWND ip_letterhead_button = CreateWindowExW(
        0, L"BUTTON", L"Бланк ИП",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin, kButtonMargin + kButtonHeight + kButtonSpacing,
        kRegistryButtonWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIpLetterheadButtonId)),
        instance, nullptr);
    if (ip_letterhead_button == nullptr)
    {
        return -1;
    }
    SendMessageW(ip_letterhead_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Бланк КИ использует отдельную команду, чтобы параметры документа не смешивались
    // с маршрутизацией сообщений главного окна.
    HWND ki_letterhead_button = CreateWindowExW(
        0, L"BUTTON", L"Бланк КИ",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin, kButtonMargin + 2 * (kButtonHeight + kButtonSpacing),
        kRegistryButtonWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kKiLetterheadButtonId)),
        instance, nullptr);
    if (ki_letterhead_button == nullptr)
    {
        return -1;
    }
    SendMessageW(ki_letterhead_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Обе территории имеют одинаковые рамки; между ними сохраняется зазор 8 пикселей.
    // Собственные ID рамок не пересекаются с ID четырёх команд открытия документов.
    constexpr int pzz_groups_top = kButtonMargin + 3 * (kButtonHeight + kButtonSpacing);
    if (!CreatePzzGroup(window, instance, L"61-48", kPzz6148GroupId,
            kPzz6148TextButtonId, kPzz6148GraphicsButtonId, pzz_groups_top))
    {
        return -1;
    }
    if (!CreatePzzGroup(window, instance, L"61-41", kPzz6141GroupId,
            kPzz6141TextButtonId, kPzz6141GraphicsButtonId,
            pzz_groups_top + kPzzGroupHeight + kButtonSpacing))
    {
        return -1;
    }

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
    tray_icon_available = TrayIcon::Add(window, instance);
    if (!tray_icon_available)
    {
        MessageBoxW(window,
            L"Не удалось добавить значок приложения в трей.\n"
            L"При сворачивании окно останется на панели задач.",
            MainWindow::kTitle, MB_OK | MB_ICONWARNING);
    }
    // КОНЕЦ: добавление иконки приложения в трей.
    // НАЧАЛО: регистрация глобальных сочетаний клавиш.
    if (!WindowHotkeys::Register(window))
    {
        MessageBoxW(window,
            L"Не удалось зарегистрировать Ctrl+Alt+M.\n"
            L"Сочетание может быть занято другой программой.\n"
            L"Окно можно восстановить через панель задач или значок в трее.",
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
    if (LOWORD(w_param) == kContractRegistryButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kContractRegistryButtonId))
    {
        ContractRegistry::Open(window);
        return true;
    }
    if (LOWORD(w_param) == kIpLetterheadButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kIpLetterheadButtonId))
    {
        IpLetterhead::Open(window);
        return true;
    }
    if (LOWORD(w_param) == kKiLetterheadButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kKiLetterheadButtonId))
    {
        KiLetterhead::Open(window);
        return true;
    }
    if (LOWORD(w_param) == kPzz6148TextButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kPzz6148TextButtonId))
    {
        Pzz6148Text::Open(window);
        return true;
    }
    if (LOWORD(w_param) == kPzz6141TextButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kPzz6141TextButtonId))
    {
        Pzz6141Text::Open(window);
        return true;
    }
    if (LOWORD(w_param) == kPzz6148GraphicsButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kPzz6148GraphicsButtonId))
    {
        Pzz6148Graphics::Open(window);
        return true;
    }
    if (LOWORD(w_param) == kPzz6141GraphicsButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kPzz6141GraphicsButtonId))
    {
        Pzz6141Graphics::Open(window);
        return true;
    }
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

void MainWindow::Minimize(HWND window)
{
    if (tray_icon_available)
    {
        // Скрытое окно исчезает с панели задач и из Alt+Tab.
        ShowWindow(window, SW_HIDE);
    }
    else
    {
        // Без значка в трее оставляем свёрнутое окно на панели задач.
        ShowWindow(window, SW_MINIMIZE);
    }
}

void MainWindow::ToggleVisibility(HWND window)
{
    if (!IsWindowVisible(window) || IsIconic(window))
    {
        Show(window);
    }
    else
    {
        Minimize(window);
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
        tray_icon_available = TrayIcon::Add(window,
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(window, GWLP_HINSTANCE)));
        if (!tray_icon_available && !IsWindowVisible(window))
        {
            // Значок исчез вместе с Проводником: возвращаем скрытое окно на панель задач.
            ShowWindow(window, SW_SHOWMINNOACTIVE);
        }
        return 0;
    }

    switch (message)
    {
    case WM_CREATE:
        return OnCreate(window, reinterpret_cast<LPCREATESTRUCTW>(l_param)->hInstance);
    case WM_SIZE:
        // Обычная кнопка сворачивания скрывает окно, только если доступен трей.
        if (w_param == SIZE_MINIMIZED && tray_icon_available)
        {
            Minimize(window);
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
