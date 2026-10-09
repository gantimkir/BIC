#include "ki_letterhead.h"

#include "office_document.h"

// Команда задаёт документ и размещение; COM и окно Word обрабатывает общий модуль.
void KiLetterhead::Open(HWND owner)
{
    const OfficeDocument::OpenOptions options{
        .path = L"C:\\YD\\ИП Гантимуров КВ\\КИ Гантимуров КВ - бланк 2.docx",
        .application = OfficeDocument::Application::Word,
        .window = {.placement = OfficeDocument::Placement::LeftHalf},
    };
    const auto result = OfficeDocument::Open(options);
    if (!result)
    {
        const auto message = OfficeDocument::DescribeError(result);
        MessageBoxW(owner, message.c_str(), L"Бланк КИ", MB_OK | MB_ICONERROR);
    }
}
