#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>

namespace Pdf
{
// Результат обработки одного выбранного PDF. Путь остаётся доступен для будущих
// действий над текущим файлом; отсутствие size_bytes/page_count означает, что
// соответствующие сведения не получены. Коды ошибок относятся только к этапам,
// которые были запущены; каталог отмечается отдельно от ошибки Win32.
struct FileInfo
{
    std::wstring path;
    std::optional<std::uint64_t> size_bytes;
    std::optional<int> page_count;
    DWORD file_error = ERROR_SUCCESS;
    DWORD page_error = ERROR_SUCCESS;
    bool is_directory = false;
    // Рабочий поток готовит текст заранее: оконная процедура лишь выбирает запись
    // и не выделяет память при переключении кнопками.
    std::wstring display_text;
};
}
