#include "pdf_panel.h"

#include "file_drop_area.h"

namespace
{
// ID остаются прежними: внешний код и оконные сообщения сохраняют идентичность.
constexpr int kGroupId = 11;
constexpr int kDropAreaId = 12;
constexpr int kTopInset = 13;
constexpr int kBottomInset = 3;
}

bool PdfPanel::Create(HWND owner, HINSTANCE instance, int content_left, int top,
    int content_width, int height, int side_overhang)
{
    // Рамка и область — соседние дочерние окна главной формы. Рамка только
    // обозначает группу; взаимодействие с файлами остаётся в FileDropArea.
    HWND group = CreateWindowExW(0, L"BUTTON", L"PDF",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_GROUPBOX,
        content_left - side_overhang, top, content_width + 2 * side_overhang, height, owner,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGroupId)), instance, nullptr);
    if (group == nullptr)
    {
        return false;
    }
    SendMessageW(group, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Интерактивная область сохраняет общие края кнопок. При ошибке её создания
    // возвращаем false, чтобы главное окно не оставляло пустую рамку.
    return FileDropArea::Create(owner, instance, kDropAreaId,
        content_left, top + kTopInset,
        content_width, height - kTopInset - kBottomInset) != nullptr;
}
