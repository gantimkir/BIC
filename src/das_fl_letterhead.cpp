#include "das_fl_letterhead.h"

#include "office_document.h"

// Команда хранит путь и параметры Excel. Открытие, повторное использование книги,
// безопасность макросов и размещение окна выполняет общий модуль OfficeDocument.
void DasFlLetterhead::Open(HWND owner, const RECT& bounds)
{
    const OfficeDocument::OpenOptions options{
        .path = L"C:\\YD\\ИП Гантимуров КВ\\Договоры\\ОБРАЗЦЫ\\Шаблон ФЛ №1 - 7.дог.xlsm",
        .application = OfficeDocument::Application::Excel,
        .window = {.placement = OfficeDocument::Placement::Rectangle, .rectangle = bounds},
    };
    const auto result = OfficeDocument::Open(options);
    if (!result)
    {
        const auto message = OfficeDocument::DescribeError(result);
        MessageBoxW(owner, message.c_str(), L"Бланк ДАС. ФЛ", MB_OK | MB_ICONERROR);
    }
}
