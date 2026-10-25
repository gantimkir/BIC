#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Pdf
{
// Результат обработки одного выбранного PDF. Путь остаётся доступен для будущих
// действий над текущим файлом; отсутствие size_bytes/page_count/raster_page_count означает, что
// соответствующие сведения не получены. Коды ошибок относятся только к этапам,
// которые были запущены; каталог отмечается отдельно от ошибки Win32.
struct FileInfo
{
    std::wstring path;
    std::optional<std::uint64_t> size_bytes;
    std::optional<int> page_count;
    // Только изображения в содержимом страницы, включая вложенные Form XObject.
    // Невидимый OCR-текст и векторный фон исключают страницу. 0 и отсутствие
    // результата различаются; аннотации и площадь покрытия не анализируются.
    std::optional<int> raster_page_count;
    // По одному флагу на страницу: 1 — уже растровая, 0 — нужна растеризация.
    // Вектор публикуется только после полного успешного анализа.
    std::vector<unsigned char> raster_pages;
    // Идентичность и время записи исходника для отказа от устаревшего анализа
    // перед созданием неизменяемого снимка на время сравнения и сохранения.
    std::optional<BY_HANDLE_FILE_INFORMATION> source_identity;
    DWORD file_error = ERROR_SUCCESS;
    DWORD page_error = ERROR_SUCCESS;
    DWORD raster_error = ERROR_SUCCESS;
    unsigned long raster_pdfium_error = 0; // Дополнительный код отказа PDFium.
    bool is_directory = false;
    // Рабочий поток готовит текст заранее: оконная процедура лишь выбирает запись
    // и не выделяет память при переключении кнопками.
    std::wstring display_text;
};
}
