#include "pdf_panel.h"

#include "file_drop_area.h"

namespace
{
// ID остаются прежними: внешний код и оконные сообщения сохраняют идентичность.
constexpr int kGroupId = 11;
constexpr int kDropAreaId = 12;
constexpr int kSideInset = 4;
constexpr int kTopInset = 13;
constexpr int kBottomInset = 3;
}

bool PdfPanel::Create(HWND owner, HINSTANCE instance, int left, int top, int width, int height)
{
    // Рамка и область — соседние дочерние окна главной формы. Рамка только
    // обозначает группу; взаимодействие с файлами остаётся в FileDropArea.
    HWND group = CreateWindowExW(0, L"BUTTON", L"PDF",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_GROUPBOX,
        left, top, width, height, owner,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGroupId)), instance, nullptr);
    if (group == nullptr)
    {
        return false;
    }
    SendMessageW(group, WM_SETFONT,
        reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    // Отступы совпадают с рамками ПЗЗ; при ошибке создания область не заменяем
    // пустой интерактивной рамкой, а сообщаем главному окну об отказе OnCreate.
    return FileDropArea::Create(owner, instance, kDropAreaId,
        left + kSideInset, top + kTopInset,
        width - 2 * kSideInset, height - kTopInset - kBottomInset) != nullptr;
}
