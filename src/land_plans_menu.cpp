#include "land_plans_menu.h"
#include "land_plans_config.h"

namespace
{
// Только разделители рисуются вручную; обычные пункты сохраняют системный вид.
const std::vector<LandPlansConfig::Entry>* active_entries = nullptr;
struct MenuGuard
{
    HMENU handle = CreatePopupMenu();
    ~MenuGuard() { if (handle) DestroyMenu(handle); }
};
struct MenuFont
{
    HFONT handle = nullptr;
    MenuFont()
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
            handle = CreateFontIndirectW(&metrics.lfMenuFont);
    }
    ~MenuFont() { if (handle) DeleteObject(handle); }
};
const LandPlansConfig::Entry* Separator(ULONG_PTR data)
{
    if (active_entries)
        for (const auto& entry : *active_entries)
            if (entry.separator && reinterpret_cast<ULONG_PTR>(&entry) == data) return &entry;
    return nullptr;
}
}

// Отрезки рисуются в физических пикселях: их длина ровно 10, зазор до текста — 6.
// DrawText с NOPREFIX сохраняет амперсанд как обычный символ заголовка.
void LandPlansMenu::DrawSeparator(HDC dc, RECT bounds, const std::wstring& text, HFONT font)
{
    const int saved = SaveDC(dc);
    if (!saved) return;
    SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    SIZE extent{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &extent);
    const int available = bounds.right - bounds.left - 40;
    const int text_width = extent.cx < available ? extent.cx : (available > 0 ? available : 0);
    const int left = bounds.left + (bounds.right - bounds.left - text_width - 32) / 2;
    const int middle = (bounds.top + bounds.bottom) / 2;
    RECT line{left, middle, left + 10, middle + 1};
    FillRect(dc, &line, GetSysColorBrush(COLOR_GRAYTEXT));
    line.left = left + 16 + text_width + 6;
    line.right = line.left + 10;
    FillRect(dc, &line, GetSysColorBrush(COLOR_GRAYTEXT));
    RECT label{left + 16, bounds.top, left + 16 + text_width, bounds.bottom};
    DrawTextW(dc, text.c_str(), -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    RestoreDC(dc, saved);
}

bool LandPlansMenu::MeasureSeparator(MEASUREITEMSTRUCT& item)
{
    if (item.CtlType != ODT_MENU) return false;
    const auto* entry = Separator(item.itemData);
    if (!entry) return false;
    MenuFont font;
    HDC dc = GetDC(nullptr);
    SIZE size{120, 20};
    if (dc)
    {
        const auto previous = SelectObject(dc, font.handle ? font.handle : GetStockObject(DEFAULT_GUI_FONT));
        GetTextExtentPoint32W(dc, entry->value.c_str(), static_cast<int>(entry->value.size()), &size);
        SelectObject(dc, previous);
        ReleaseDC(nullptr, dc);
    }
    item.itemWidth = static_cast<UINT>(size.cx + 48);
    item.itemHeight = static_cast<UINT>(size.cy + 10);
    return true;
}

bool LandPlansMenu::DrawMenuSeparator(const DRAWITEMSTRUCT& item)
{
    if (item.CtlType != ODT_MENU) return false;
    const auto* entry = Separator(item.itemData);
    if (!entry) return false;
    FillRect(item.hDC, &item.rcItem, GetSysColorBrush(COLOR_MENU));
    MenuFont font;
    DrawSeparator(item.hDC, item.rcItem, entry->value, font.handle);
    return true;
}

void LandPlansMenu::Show(HWND owner, HWND button, const RECT& opening_bounds)
{
    LandPlansConfig::Snapshot snapshot;
    std::wstring error;
    if (!LandPlansConfig::Load(snapshot, error))
    {
        MessageBoxW(owner, error.c_str(), L"Папки", MB_OK | MB_ICONERROR);
        return;
    }
    auto& entries = snapshot.entries;
    LandPlansConfig::Sort(entries);
    if (entries.empty())
    {
        MessageBoxW(owner, L"Список пуст. Добавьте папки через кнопку «...» справа от «Папки».",
            L"Папки", MB_OK | MB_ICONINFORMATION);
        return;
    }
    // Ссылки itemData действуют до завершения синхронного TrackPopupMenuEx.
    struct ActiveGuard
    {
        const std::vector<LandPlansConfig::Entry>* previous = active_entries;
        ~ActiveGuard() { active_entries = previous; }
    } active_guard;
    active_entries = &entries;
    MenuGuard menu;
    bool created = menu.handle != nullptr;
    for (size_t i = 0; created && i < entries.size(); ++i)
    {
        const auto& entry = entries[i];
        if (entry.separator)
        {
            // Отключённый owner-draw пункт — заголовок, он не открывает папку.
            created = AppendMenuW(menu.handle, MF_OWNERDRAW | MF_DISABLED | MF_GRAYED,
                static_cast<UINT_PTR>(i + 1), reinterpret_cast<LPCWSTR>(&entry)) != FALSE;
        }
        else
        {
            std::wstring caption;
            for (const wchar_t ch : LandPlansConfig::Label(entry))
            {
                caption += ch;
                if (ch == L'&') caption += L'&';
            }
            created = AppendMenuW(menu.handle, MF_STRING, static_cast<UINT_PTR>(i + 1), caption.c_str()) != FALSE;
        }
    }
    RECT bounds{};
    if (!created || !GetWindowRect(button, &bounds))
    {
        MessageBoxW(owner, L"Не удалось создать или разместить меню.", L"Папки", MB_OK | MB_ICONERROR);
        return;
    }
    TPMPARAMS parameters{};
    parameters.cbSize = sizeof(parameters);
    parameters.rcExclude = bounds;
    // Временные ID не попадают в WM_COMMAND главного окна; ноль означает отмену.
    const UINT selected = TrackPopupMenuEx(menu.handle, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON |
        TPM_RETURNCMD | TPM_NONOTIFY, bounds.left, bounds.bottom, owner, &parameters);
    if (selected && selected <= entries.size() && !entries[selected - 1].separator)
        ExplorerFolder::Open(owner, entries[selected - 1].value, opening_bounds);
}
