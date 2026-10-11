#include "pzz_61_41_text.h"

#include "office_document.h"

// Команда хранит путь и способ размещения документа; COM и окно Word обрабатывает
// общий модуль, поэтому обработчик главного окна не дублирует Office-логику.
void Pzz6141Text::Open(HWND owner)
{
    const OfficeDocument::OpenOptions options{
        .path = L"C:\\YD\\Межевые планы\\ПЗЗ\\Цимлянск\\ПЗЗ 61-41.docx",
        .application = OfficeDocument::Application::Word,
        .window = {.placement = OfficeDocument::Placement::LeftHalf},
    };
    const auto result = OfficeDocument::Open(options);
    if (!result)
    {
        const auto message = OfficeDocument::DescribeError(result);
        MessageBoxW(owner, message.c_str(), L"ПЗЗ 61-41. Текст", MB_OK | MB_ICONERROR);
    }
}
