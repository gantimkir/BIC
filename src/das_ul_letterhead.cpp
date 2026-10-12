#include "das_ul_letterhead.h"

#include "office_document.h"

#include <string>

namespace
{
// Все операции Office остаются в общем модуле. Собираем ошибки по каждому файлу,
// чтобы отказ Word или одной книги Excel не мешал открыть остальные документы.
void OpenOne(const wchar_t* path, OfficeDocument::Application application,
    const RECT& bounds, std::wstring& errors)
{
    const OfficeDocument::OpenOptions options{
        .path = path,
        .application = application,
        .window = {.placement = OfficeDocument::Placement::Rectangle, .rectangle = bounds},
    };
    const auto result = OfficeDocument::Open(options);
    if (!result)
    {
        if (!errors.empty())
        {
            errors += L"\n\n";
        }
        errors += path;
        errors += L"\n";
        errors += OfficeDocument::DescribeError(result);
    }
}
} // namespace

void DasUlLetterhead::Open(HWND owner)
{
    // SPI_GETWORKAREA возвращает рабочую область основного монитора без панели задач,
    // в той же системе экранных координат, что и Rectangle в OfficeDocument.
    RECT work{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
    {
        const DWORD error = GetLastError();
        const std::wstring message = L"Не удалось определить рабочую область экрана.\nКод ошибки: " +
            std::to_wstring(error);
        MessageBoxW(owner, message.c_str(), L"Бланк ДАС. ЮЛ", MB_OK | MB_ICONERROR);
        return;
    }
    if (work.right - work.left < 2 || work.bottom - work.top < 2)
    {
        MessageBoxW(owner, L"Рабочая область экрана слишком мала для размещения окон.",
            L"Бланк ДАС. ЮЛ", MB_OK | MB_ICONERROR);
        return;
    }
    const LONG middle_x = work.left + (work.right - work.left) / 2;
    const LONG middle_y = work.top + (work.bottom - work.top) / 2;
    // Общие границы четвертей исключают щели при нечётной ширине или высоте экрана.
    const RECT lower_left{work.left, middle_y, middle_x, work.bottom};
    const RECT upper_left{work.left, work.top, middle_x, middle_y};
    const RECT upper_right{middle_x, work.top, work.right, middle_y};

    std::wstring errors;
    OpenOne(L"C:\\YD\\ИП Гантимуров КВ\\Договоры\\ОБРАЗЦЫ\\Шаблон ЮЛ №1-1 - дог.doc",
        OfficeDocument::Application::Word, lower_left, errors);
    OpenOne(L"C:\\YD\\ИП Гантимуров КВ\\Договоры\\ОБРАЗЦЫ\\Шаблон ЮЛ №1-1 - смета.xlsx",
        OfficeDocument::Application::Excel, upper_left, errors);
    OpenOne(L"C:\\YD\\ИП Гантимуров КВ\\Договоры\\ОБРАЗЦЫ\\Шаблон ЮЛ №1-1 - акт.xlsx",
        OfficeDocument::Application::Excel, upper_right, errors);
    if (!errors.empty())
    {
        MessageBoxW(owner, errors.c_str(), L"Бланк ДАС. ЮЛ", MB_OK | MB_ICONERROR);
    }
}
