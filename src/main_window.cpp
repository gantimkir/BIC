#include "main_window.h"
#include "resource.h"
#include <cwchar>

#include "tray_icon.h"
#include "window_hotkeys.h"
#include "contract_registry.h"
#include "das_fl_letterhead.h"
#include "land_plans_menu.h"
#include "land_plans_editor.h"
#include "explorer_folder.h"
#include "das_ul_letterhead.h"
#include "ip_letterhead.h"
#include "ki_letterhead.h"
#include "pzz_61_48_text.h"
#include "pzz_61_41_text.h"
#include "pzz_61_48_graphics.h"
#include "pzz_61_41_graphics.h"
#include "pdf/pdf_panel.h"

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
constexpr int kDasFlLetterheadButtonId = 13;
constexpr int kDasUlLetterheadButtonId = 14;
constexpr int kCompactButtonId = 15;
constexpr int kLandPlansButtonId = 16;
constexpr int kLandPlansEditorId = 23;
// 153/255 = 60% непрозрачности; применяется ко всему компактному окну.
constexpr BYTE kCompactAlpha = 153;
constexpr int kButtonWidth = 100;
constexpr int kButtonHeight = 32;
constexpr int kButtonMargin = 16;
constexpr int kButtonSpacing = 8;
// Одиночные команды и пары имеют одинаковые наружные края в своей колонке.
// Рамки ПЗЗ/PDF выступают за эти края, не сужая интерактивные элементы.
constexpr int kPairedButtonsWidth = 180;
constexpr int kPairGap = 4;
constexpr int kCommandRowWidth = kPairedButtonsWidth + kPairGap;
constexpr int kRightColumnLeft = kButtonMargin + kCommandRowWidth + kButtonSpacing;
constexpr int kFrameOverhang = 4;
// Надпись рамки занимает верхние 13 пикселей, нижняя граница — ещё 3 пикселя.
constexpr int kPzzGroupTopInset = 13;
constexpr int kPzzGroupBottomInset = 3;
constexpr int kPzzGroupHeight = kPzzGroupTopInset + kButtonHeight + kPzzGroupBottomInset;
constexpr int kPzzGroupButtonWidth = kPairedButtonsWidth / 2;
bool tray_icon_available = false;
// Запоминаем только закрепление, введённое выбором правой половины. Если BIC
// уже был topmost по другой причине, смена варианта не должна снимать этот режим.
bool placement_added_topmost = false;

// Снимок полного окна хранится на UI-потоке и обновляется перед каждым сжатием.
// WINDOWPLACEMENT используется для отката при ошибке сжатия; обычное раскрытие — в четверть экрана.
bool compact_mode = false;
bool compact_transition = false;
WINDOWPLACEMENT expanded_placement{};
LONG_PTR expanded_style = 0;
LONG_PTR expanded_ex_style = 0;
// Если полное окно уже было layered, восстанавливаем его прежние атрибуты.
COLORREF expanded_color_key = 0;
BYTE expanded_alpha = 255;
DWORD expanded_layered_flags = LWA_ALPHA;

// Создаёт рамку вокруг общего ряда и две команды ПЗЗ шириной 90 пикселей каждая.
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
        kButtonMargin - kFrameOverhang, top,
        kCommandRowWidth + 2 * kFrameOverhang, kPzzGroupHeight, window,
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
        kButtonMargin, top + kPzzGroupTopInset,
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
        kButtonMargin + kPzzGroupButtonWidth + kPairGap,
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
    // Бланки делят ширину поровну. В ряду папок ширина распределяется
    // в пропорции 61,8% / остаток, без учёта общего зазора в 4 пикселя.
    constexpr int half_button_width = kPairedButtonsWidth / 2;
    constexpr int folders_button_width = (kPairedButtonsWidth * 618 + 500) / 1000;
    constexpr int folders_editor_width = kPairedButtonsWidth - folders_button_width;
    constexpr int pzz_groups_top = kButtonMargin + 2 * (kButtonHeight + kButtonSpacing);
    constexpr int pdf_top = pzz_groups_top + 2 * (kPzzGroupHeight + kButtonSpacing);
    constexpr int folders_top = pdf_top + kPzzGroupHeight + kButtonSpacing;
    HWND registry_button = CreateWindowExW(
        0, L"BUTTON", L"Реестр договоров",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin, kButtonMargin, kCommandRowWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kContractRegistryButtonId)),
        instance, nullptr);
    if (registry_button == nullptr)
    {
        return -1;
    }
    SendMessageW(registry_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Вторая кнопка верхнего ряда повторяет размер реестра; справа оставляем
    // общий зазор 8 пикселей. Отдельный ID сохраняет независимую команду Excel.
    HWND das_fl_letterhead_button = CreateWindowExW(
        0, L"BUTTON", L"Бланк ДАС. ФЛ",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kRightColumnLeft, kButtonMargin, kCommandRowWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDasFlLetterheadButtonId)),
        instance, nullptr);
    if (das_fl_letterhead_button == nullptr)
    {
        return -1;
    }
    SendMessageW(das_fl_letterhead_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Под PDF находятся меню папок и редактор списка; меню привязано к новой позиции.
    HWND land_plans_button = CreateWindowExW(
        0, L"BUTTON", L"Папки. Кадастр",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin, folders_top, folders_button_width, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLandPlansButtonId)),
        instance, nullptr);
    if (land_plans_button == nullptr)
    {
        return -1;
    }
    SendMessageW(land_plans_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Редактор занимает остаток ряда; меню открывается от новой широкой кнопки.
    HWND editor_button = CreateWindowExW(
        0, L"BUTTON", L"...",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin + folders_button_width + kPairGap, folders_top,
        folders_editor_width, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLandPlansEditorId)), instance, nullptr);
    if (editor_button == nullptr) return -1;
    SendMessageW(editor_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // ЮЛ продолжает правую колонку под ФЛ; размер и вертикальный зазор совпадают
    // с остальными кнопками. Команда открывает три документа через общий модуль.
    HWND das_ul_letterhead_button = CreateWindowExW(
        0, L"BUTTON", L"Бланк ДАС. ЮЛ",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kRightColumnLeft,
        kButtonMargin + kButtonHeight + kButtonSpacing,
        kCommandRowWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDasUlLetterheadButtonId)),
        instance, nullptr);
    if (das_ul_letterhead_button == nullptr)
    {
        return -1;
    }
    SendMessageW(das_ul_letterhead_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // ИП и КИ стоят рядом под реестром; каждый сохраняет независимую команду.
    HWND ip_letterhead_button = CreateWindowExW(
        0, L"BUTTON", L"Бланк ИП",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin, kButtonMargin + kButtonHeight + kButtonSpacing,
        half_button_width, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIpLetterheadButtonId)),
        instance, nullptr);
    if (ip_letterhead_button == nullptr)
    {
        return -1;
    }
    SendMessageW(ip_letterhead_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // КИ занимает правую половину ряда бланков; высота и верх совпадают с ИП.
    HWND ki_letterhead_button = CreateWindowExW(
        0, L"BUTTON", L"Бланк КИ",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        kButtonMargin + half_button_width + kPairGap, kButtonMargin + kButtonHeight + kButtonSpacing,
        half_button_width, kButtonHeight, window,
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

    // PDF-модуль создаёт собственную рамку и область выбора; главная форма
    // передаёт края интерактивного ряда и общий выступ рамки.
    if (!PdfPanel::Create(window, instance, kButtonMargin, pdf_top,
            kCommandRowWidth, kPzzGroupHeight, kFrameOverhang))
    {
        return -1;
    }

    // В обычном режиме эта кнопка находится над «Закрыть», а в компактном
    // занимает всю клиентскую область и остаётся единственным видимым элементом.
    HWND compact_button = CreateWindowExW(
        0, L"BUTTON", L"Компакт",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, kButtonWidth, kButtonHeight, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCompactButtonId)), instance, nullptr);
    if (compact_button == nullptr)
    {
        return -1;
    }
    SendMessageW(compact_button, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

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
    if (compact_mode)
    {
        // При смене стилей WM_SIZE может прийти несколько раз; кнопка всегда
        // покрывает текущую клиентскую область, не оставляя скрытых отступов.
        MoveWindow(GetDlgItem(window, kCompactButtonId), 0, 0,
            client.right, client.bottom, TRUE);
        return;
    }
    const int left = client.right > kButtonWidth + kButtonMargin
        ? client.right - kButtonWidth - kButtonMargin : 0;
    const int close_top = client.bottom > kButtonHeight + kButtonMargin
        ? client.bottom - kButtonHeight - kButtonMargin : 0;
    const int compact_top = close_top > kButtonHeight + kButtonSpacing
        ? close_top - kButtonHeight - kButtonSpacing : 0;
    MoveWindow(GetDlgItem(window, kCloseButtonId), left, close_top,
        kButtonWidth, kButtonHeight, TRUE);
    MoveWindow(GetDlgItem(window, kCompactButtonId), left, compact_top,
        kButtonWidth, kButtonHeight, TRUE);
}

void ShowCompactError(HWND owner, const wchar_t* action, DWORD error)
{
    wchar_t message[256]{};
    swprintf_s(message, L"%s\nКод ошибки: %lu", action, error);
    MessageBoxW(owner, message, MainWindow::kTitle, MB_OK | MB_ICONERROR);
}

// Нулевой результат SetWindowLongPtrW допустим для ранее нулевого extended style.
// Поэтому ошибку определяем только после предварительного обнуления LastError.
bool SetWindowValue(HWND window, int index, LONG_PTR value)
{
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(window, index, value);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

// В обычном режиме кнопка рисуется Windows. Красный owner-draw включаем только
// в компактном; смена типа не меняет её ID и обработчик BN_CLICKED.
bool SetCompactButtonAppearance(HWND window, bool compact)
{
    HWND button = GetDlgItem(window, kCompactButtonId);
    const LONG_PTR style = GetWindowLongPtrW(button, GWL_STYLE);
    const LONG_PTR type = compact ? BS_OWNERDRAW : BS_PUSHBUTTON;
    if (!SetWindowValue(button, GWL_STYLE, (style & ~static_cast<LONG_PTR>(BS_TYPEMASK)) | type))
    {
        return false;
    }
    InvalidateRect(button, nullptr, TRUE);
    return true;
}

bool DrawCompactButton(const DRAWITEMSTRUCT& item)
{
    if (item.CtlType != ODT_BUTTON || item.CtlID != kCompactButtonId)
    {
        return false;
    }
    const int saved_dc = SaveDC(item.hDC);
    if (saved_dc == 0)
    {
        return true;
    }
    // Stock DC_BRUSH не требует выделения/удаления GDI-объекта при каждой отрисовке.
    // Светло-красный фон слегка темнеет при нажатии; тёмная надпись сохраняет контраст.
    const COLORREF background = compact_mode
        ? ((item.itemState & ODS_SELECTED) != 0 ? RGB(235, 145, 145) : RGB(255, 170, 170))
        : GetSysColor(COLOR_BTNFACE);
    SetDCBrushColor(item.hDC, background);
    FillRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0));
    SelectObject(item.hDC, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, compact_mode ? RGB(60, 0, 0) : GetSysColor(COLOR_BTNTEXT));
    wchar_t caption[32]{};
    GetWindowTextW(item.hwndItem, caption, static_cast<int>(sizeof(caption) / sizeof(caption[0])));
    RECT text_bounds = item.rcItem;
    if ((item.itemState & ODS_SELECTED) != 0)
    {
        OffsetRect(&text_bounds, 1, 1);
    }
    DrawTextW(item.hDC, caption, -1, &text_bounds, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((item.itemState & ODS_FOCUS) != 0 && (item.itemState & ODS_NOFOCUSRECT) == 0)
    {
        RECT focus_bounds = item.rcItem;
        InflateRect(&focus_bounds, -3, -3);
        DrawFocusRect(item.hDC, &focus_bounds);
    }
    RestoreDC(item.hDC, saved_dc);
    return true;
}

bool ApplyFrame(HWND window, LONG_PTR style, LONG_PTR ex_style)
{
    const LONG_PTR previous_style = GetWindowLongPtrW(window, GWL_STYLE);
    if (!SetWindowValue(window, GWL_STYLE, style))
    {
        return false;
    }
    if (!SetWindowValue(window, GWL_EXSTYLE, ex_style))
    {
        const DWORD error = GetLastError();
        SetWindowValue(window, GWL_STYLE, previous_style);
        SetLastError(error);
        return false;
    }
    return true;
}

LONG_PTR CompactStyle()
{
    return (expanded_style & ~static_cast<LONG_PTR>(WS_OVERLAPPEDWINDOW)) |
        static_cast<LONG_PTR>(WS_POPUP | WS_CAPTION | WS_SYSMENU);
}

LONG_PTR CompactExStyle()
{
    // Расширенные края тоже занимают место. Убираем их лишь на время сжатия.
    return (expanded_ex_style & ~static_cast<LONG_PTR>(WS_EX_WINDOWEDGE |
        WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE)) | WS_EX_LAYERED;
}

void ShowOtherControls(HWND window, bool visible)
{
    const HWND compact_button = GetDlgItem(window, kCompactButtonId);
    // Текущие элементы формы — прямые дочерние окна, в полном режиме все видимы.
    // Не уничтожаем их: состояние PDF, регистрация drop и ID команд сохраняются.
    for (HWND child = GetWindow(window, GW_CHILD); child != nullptr;
            child = GetWindow(child, GW_HWNDNEXT))
    {
        if (child != compact_button)
        {
            ShowWindow(child, visible ? SW_SHOW : SW_HIDE);
        }
    }
}

// Обычное раскрытие возвращает правую нижнюю четверть основного монитора.
// При отказе входа в компактный режим to_quarter=false откатывает старый снимок,
// чтобы не менять положение окна в результате неуспешной операции.
bool RestoreExpandedWindow(HWND window, bool to_quarter = true)
{
    RECT compact_bounds{};
    if (!GetWindowRect(window, &compact_bounds))
    {
        ShowCompactError(window, L"Не удалось получить положение компактного окна.", GetLastError());
        return false;
    }
    RECT target_bounds{};
    if (to_quarter)
    {
        RECT work{};
        if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
        {
            ShowCompactError(window, L"Не удалось определить рабочую область экрана.", GetLastError());
            return false;
        }
        if (work.right - work.left < 2 || work.bottom - work.top < 2)
        {
            ShowCompactError(window, L"Рабочая область слишком мала для раскрытия окна.", ERROR_INVALID_DATA);
            return false;
        }
        // Рабочая область исключает панель задач; её смещение тоже учитывается.
        target_bounds = RECT{work.left + (work.right - work.left) / 2,
            work.top + (work.bottom - work.top) / 2, work.right, work.bottom};
    }
    if (!ApplyFrame(window, expanded_style, expanded_ex_style))
    {
        ShowCompactError(window, L"Не удалось восстановить рамку окна.", GetLastError());
        return false;
    }
    const bool restored_alpha = (expanded_ex_style & WS_EX_LAYERED) == 0 ||
        SetLayeredWindowAttributes(window, expanded_color_key, expanded_alpha, expanded_layered_flags);
    const HWND restored_z_order = (expanded_ex_style & WS_EX_TOPMOST) != 0 ? HWND_TOPMOST : HWND_NOTOPMOST;
    // WM_SIZE во время расширения уже должен раскладывать обычные кнопки,
    // иначе owner-draw «Раскрыть» временно закрасит всю большую клиентскую область.
    compact_mode = false;
    const bool placed = restored_alpha && (to_quarter
        ? SetWindowPos(window, restored_z_order, target_bounds.left, target_bounds.top,
            target_bounds.right - target_bounds.left, target_bounds.bottom - target_bounds.top,
            SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW) != FALSE
        : SetWindowPlacement(window, &expanded_placement) != FALSE);
    if (!placed)
    {
        const DWORD error = GetLastError();
        compact_mode = true;
        // Возвращаем точный внешний размер компактного окна вместе с заголовком,
        // а не размер клиентской кнопки, чтобы она оставалась доступной для повторной попытки.
        ApplyFrame(window, CompactStyle(), CompactExStyle());
        SetLayeredWindowAttributes(window, 0, kCompactAlpha, LWA_ALPHA);
        SetWindowPos(window, HWND_TOPMOST, compact_bounds.left, compact_bounds.top,
            compact_bounds.right - compact_bounds.left, compact_bounds.bottom - compact_bounds.top,
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
        OnResize(window);
        ShowCompactError(window, L"Не удалось раскрыть окно.", error);
        return false;
    }
    if (!SetCompactButtonAppearance(window, false))
    {
        ShowCompactError(window, L"Не удалось восстановить вид кнопки.", GetLastError());
    }
    SetWindowTextW(GetDlgItem(window, kCompactButtonId), L"Компакт");
    ShowOtherControls(window, true);
    // NOZORDER не используется: снимаем временное закрепление поверх окон.
    if (!SetWindowPos(window, restored_z_order, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED))
    {
        ShowCompactError(window, L"Не удалось обновить рамку раскрытого окна.", GetLastError());
    }
    OnResize(window);
    SetFocus(GetDlgItem(window, kCompactButtonId));
    return true;
}

void ToggleCompact(HWND window)
{
    if (compact_transition)
    {
        return;
    }
    // Вызовы Win32 могут повторно войти в обработчик окна; второй переход не допускаем.
    compact_transition = true;
    struct TransitionGuard
    {
        ~TransitionGuard() { compact_transition = false; }
    } guard;
    if (compact_mode)
    {
        RestoreExpandedWindow(window);
        return;
    }

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    RECT button_bounds{};
    if (!GetWindowPlacement(window, &placement) ||
        !GetWindowRect(GetDlgItem(window, kCompactButtonId), &button_bounds))
    {
        ShowCompactError(window, L"Не удалось сохранить положение окна.", GetLastError());
        return;
    }
    // Динамические признаки максимизации/минимизации восстанавливаются через
    // WINDOWPLACEMENT; их нельзя переносить в стиль компактного popup-окна.
    expanded_placement = placement;
    expanded_style = GetWindowLongPtrW(window, GWL_STYLE) &
        ~static_cast<LONG_PTR>(WS_MAXIMIZE | WS_MINIMIZE);
    expanded_ex_style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((expanded_ex_style & WS_EX_LAYERED) != 0 &&
        !GetLayeredWindowAttributes(window, &expanded_color_key, &expanded_alpha, &expanded_layered_flags))
    {
        ShowCompactError(window, L"Не удалось сохранить прозрачность окна.", GetLastError());
        return;
    }
    // Заголовок нужен для стандартного перетаскивания и закрытия (SC_CLOSE).
    // Кнопка остаётся 100 x 32 в клиентской области; внешний прямоугольник больше
    // на величину стандартного заголовка/границ. Положение самой кнопки сохраняем.
    RECT compact_bounds{0, 0, kButtonWidth, kButtonHeight};
    if (!AdjustWindowRectEx(&compact_bounds, static_cast<DWORD>(CompactStyle()), FALSE,
            static_cast<DWORD>(CompactExStyle())))
    {
        ShowCompactError(window, L"Не удалось рассчитать размер компактного окна.", GetLastError());
        return;
    }
    OffsetRect(&compact_bounds, button_bounds.left, button_bounds.top);
    if (IsZoomed(window))
    {
        ShowWindow(window, SW_RESTORE);
    }
    if (!ApplyFrame(window, CompactStyle(), CompactExStyle()))
    {
        const DWORD error = GetLastError();
        SetWindowPlacement(window, &expanded_placement);
        ShowCompactError(window, L"Не удалось включить компактный режим.", error);
        return;
    }
    compact_mode = true;
    SetWindowTextW(GetDlgItem(window, kCompactButtonId), L"Раскрыть");
    ShowOtherControls(window, false);
    if (!SetCompactButtonAppearance(window, true) ||
        !SetLayeredWindowAttributes(window, 0, kCompactAlpha, LWA_ALPHA))
    {
        const DWORD error = GetLastError();
        RestoreExpandedWindow(window, false);
        ShowCompactError(window, L"Не удалось включить светло-красный полупрозрачный вид.", error);
        return;
    }
    // Размещаем кнопку на прежнем месте, стандартный заголовок — над ней.
    if (!SetWindowPos(window, HWND_TOPMOST, compact_bounds.left, compact_bounds.top,
            compact_bounds.right - compact_bounds.left, compact_bounds.bottom - compact_bounds.top,
            SWP_NOACTIVATE | SWP_FRAMECHANGED))
    {
        const DWORD error = GetLastError();
        RestoreExpandedWindow(window, false);
        ShowCompactError(window, L"Не удалось уменьшить окно.", error);
        return;
    }
    OnResize(window);
    SetFocus(GetDlgItem(window, kCompactButtonId));
}

// Правая половина перекрывает обычное положение BIC внизу справа. Держим BIC
// поверх внешнего окна, пока выбран этот вариант. При уходе с него снимаем
// только закрепление, добавленное здесь; компактный режим хранит свой Z-порядок.
bool UpdatePlacementTopmost(HWND window, int selected_id)
{
    if (selected_id == IDC_PLACEMENT_RIGHT_HALF)
    {
        if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0)
        {
            return true;
        }
        if (!SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
        {
            ShowCompactError(window, L"Не удалось закрепить BIC поверх открываемого окна.", GetLastError());
            return false;
        }
        placement_added_topmost = true;
    }
    else if (placement_added_topmost)
    {
        if (!SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
        {
            ShowCompactError(window, L"Не удалось вернуть обычное положение BIC.", GetLastError());
            return false;
        }
        placement_added_topmost = false;
    }
    return true;
}

// Возвращает ID выбранной области или IDCANCEL. До подтверждения запроса
// диалог не меняет положение главного окна поверх других.
INT_PTR CALLBACK PlacementDialogProcedure(HWND dialog, UINT message, WPARAM w_param, LPARAM)
{
    switch (message)
    {
    case WM_INITDIALOG:
    {
        RECT owner_bounds{};
        RECT dialog_bounds{};
        // Оба прямоугольника в экранных координатах. Учитываем заголовок и рамку
        // обоих окон, чтобы центрировать внешний контур, а не рабочую область экрана.
        if (GetWindowRect(GetWindow(dialog, GW_OWNER), &owner_bounds) &&
            GetWindowRect(dialog, &dialog_bounds))
        {
            const LONG x = owner_bounds.left +
                ((owner_bounds.right - owner_bounds.left) - (dialog_bounds.right - dialog_bounds.left)) / 2;
            const LONG y = owner_bounds.top +
                ((owner_bounds.bottom - owner_bounds.top) - (dialog_bounds.bottom - dialog_bounds.top)) / 2;
            SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        // Win32 создаёт все варианты неотмеченными. Каждый запрос требует
        // нового выбора; фокус клавиатуры не считается нажатием.
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(w_param) == IDCANCEL)
        {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        if (const int id = LOWORD(w_param); id >= IDC_PLACEMENT_TOP_LEFT &&
            id <= IDC_PLACEMENT_RIGHT_HALF && HIWORD(w_param) == BN_CLICKED)
        {
            EndDialog(dialog, id);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// Единый снимок области для всех внешних документов и папок. Вычисляется
// до открытия: изменение выбора во время ожидания не меняет текущую операцию.
bool GetOpeningBounds(HWND window, int placement, RECT& bounds)
{
    RECT area{};
    const bool centered = placement == IDC_PLACEMENT_CENTER;
    bool area_available = false;
    if (centered)
    {
        // Точка (0, 0) лежит на основном мониторе. «Центр» использует весь
        // экран, включая область панели задач, в отличие от половин и четвертей.
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        area_available = GetMonitorInfoW(
            MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) != FALSE;
        area = monitor.rcMonitor;
    }
    else
    {
        area_available = SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0) != FALSE;
    }
    const LONG width = area.right - area.left;
    const LONG height = area.bottom - area.top;
    if (!area_available || width < 2 || height < 2)
    {
        MessageBoxW(window, L"Не удалось определить область экрана для размещения окна.",
            L"Размещение", MB_OK | MB_ICONERROR);
        return false;
    }
    if (centered)
    {
        // Округляем обе стороны до ближайшего пикселя, оставшийся зазор делим пополам.
        const LONG target_width = MulDiv(width, 618, 1000);
        const LONG target_height = MulDiv(height, 618, 1000);
        const LONG left = area.left + (width - target_width) / 2;
        const LONG top = area.top + (height - target_height) / 2;
        bounds = {left, top, left + target_width, top + target_height};
        return true;
    }
    const LONG middle_x = area.left + width / 2;
    const LONG middle_y = area.top + height / 2;
    bounds = area;
    if (placement == IDC_PLACEMENT_TOP_RIGHT)
    {
        bounds.left = middle_x;
        bounds.bottom = middle_y;
    }
    else if (placement == IDC_PLACEMENT_RIGHT_HALF)
    {
        // Полная высота правой половины; BIC остаётся поверх этой области.
        bounds.left = middle_x;
    }
    else
    {
        bounds.right = middle_x;
        if (placement == IDC_PLACEMENT_BOTTOM_LEFT)
            bounds.top = middle_y;
        else if (placement != IDC_PLACEMENT_LEFT_HALF)
            bounds.bottom = middle_y; // По умолчанию — верхняя левая четверть.
    }
    return true;
}

// Возвращает область только после выбора в модальном окне. Отмена не запускает
// открытие файла или папки и не меняет закрепление главного окна.
bool RequestOpeningBounds(HWND window, RECT& bounds)
{
    const INT_PTR result = DialogBoxW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_PLACEMENT),
        window, PlacementDialogProcedure);
    if (result == -1)
    {
        ShowCompactError(window, L"Не удалось открыть окно «Размещение».", GetLastError());
        return false;
    }
    if (result < IDC_PLACEMENT_TOP_LEFT || result > IDC_PLACEMENT_RIGHT_HALF || !IsWindow(window))
        return false;

    const int placement = static_cast<int>(result);
    if (!GetOpeningBounds(window, placement, bounds) || !UpdatePlacementTopmost(window, placement))
        return false;
    return true;
}

bool OnCommand(HWND window, WPARAM w_param, LPARAM l_param)
{
    const int command_id = LOWORD(w_param);
    RECT opening_bounds{};
    // Для встроенных файлов сначала запрашиваем область; при отмене не доходим
    // до обработчика открытия. ЮЛ и служебные диалоги пропускают этот запрос.
    switch (command_id)
    {
    case kContractRegistryButtonId:
    case kDasFlLetterheadButtonId:
    case kIpLetterheadButtonId:
    case kKiLetterheadButtonId:
    case kPzz6148TextButtonId:
    case kPzz6141TextButtonId:
    case kPzz6148GraphicsButtonId:
    case kPzz6141GraphicsButtonId:
        if (HIWORD(w_param) == BN_CLICKED &&
            reinterpret_cast<HWND>(l_param) == GetDlgItem(window, command_id) &&
            !RequestOpeningBounds(window, opening_bounds)) return true;
        break;
    }
    if (command_id == kLandPlansEditorId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kLandPlansEditorId))
    {
        LandPlansEditor::Show(window);
        return true;
    }
    if (LOWORD(w_param) == kContractRegistryButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kContractRegistryButtonId))
    {
        ContractRegistry::Open(window, opening_bounds);
        return true;
    }
    if (LOWORD(w_param) == kDasFlLetterheadButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kDasFlLetterheadButtonId))
    {
        DasFlLetterhead::Open(window, opening_bounds);
        return true;
    }
    // Сначала закрываем меню с выбранным путём, затем спрашиваем размещение.
    // Отмена любого из двух шагов не запускает Проводник.
    if (LOWORD(w_param) == kLandPlansButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kLandPlansButtonId))
    {
        const auto path = LandPlansMenu::Select(window, GetDlgItem(window, kLandPlansButtonId));
        if (path && RequestOpeningBounds(window, opening_bounds))
            ExplorerFolder::Open(window, *path, opening_bounds);
        return true;
    }
    if (LOWORD(w_param) == kDasUlLetterheadButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kDasUlLetterheadButtonId))
    {
        DasUlLetterhead::Open(window);
        return true;
    }
    if (LOWORD(w_param) == kIpLetterheadButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kIpLetterheadButtonId))
    {
        IpLetterhead::Open(window, opening_bounds);
        return true;
    }
    if (LOWORD(w_param) == kKiLetterheadButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kKiLetterheadButtonId))
    {
        KiLetterhead::Open(window, opening_bounds);
        return true;
    }
    if (LOWORD(w_param) == kPzz6148TextButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kPzz6148TextButtonId))
    {
        Pzz6148Text::Open(window, opening_bounds);
        return true;
    }
    if (LOWORD(w_param) == kPzz6141TextButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kPzz6141TextButtonId))
    {
        Pzz6141Text::Open(window, opening_bounds);
        return true;
    }
    if (LOWORD(w_param) == kPzz6148GraphicsButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kPzz6148GraphicsButtonId))
    {
        Pzz6148Graphics::Open(window, opening_bounds);
        return true;
    }
    if (LOWORD(w_param) == kPzz6141GraphicsButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kPzz6141GraphicsButtonId))
    {
        Pzz6141Graphics::Open(window, opening_bounds);
        return true;
    }
    if (LOWORD(w_param) == kCompactButtonId && HIWORD(w_param) == BN_CLICKED &&
        reinterpret_cast<HWND>(l_param) == GetDlgItem(window, kCompactButtonId))
    {
        ToggleCompact(window);
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
    // После закрытия BIC отложенные запросы больше не перемещают окна Проводника.
    ExplorerFolder::Cancel();
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
    case WM_DRAWITEM:
    {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
        if (item != nullptr && (LandPlansMenu::DrawMenuSeparator(*item) || DrawCompactButton(*item)))
        {
            return TRUE;
        }
        break;
    }
    case WM_MEASUREITEM:
    {
        auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(l_param);
        if (item && LandPlansMenu::MeasureSeparator(*item)) return TRUE;
        break;
    }
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
