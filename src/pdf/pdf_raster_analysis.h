#pragma once

#include <windows.h>
#include <atomic>
#include <optional>
#include <vector>

namespace PdfRasterAnalysis
{
struct Result
{
    // Значение есть только после полного обхода: 0 — достоверное отсутствие
    // растровых страниц, nullopt — ошибка или отмена, но не нулевой результат.
    std::optional<int> raster_pages;
    std::vector<unsigned char> page_is_raster; // Индекс с нуля; только при полном успехе.
    DWORD error = ERROR_SUCCESS;
    unsigned long pdfium_error = 0; // Исходный код PDFium при отказе загрузки.
};

// Синхронный структурный анализ из worker. Заимствует уже проверенный локальный
// дескриптор, повторно открывает тот же файл только для чтения. expected_pages —
// число страниц Ghostscript: расхождение движков даёт ошибку, а не частичный итог.
// Растровая страница содержит изображения и не содержит текста (включая OCR),
// путей или shading; Form XObject обходятся рекурсивно. Пустая страница не считается.
// Анализируется содержимое страницы, без отдельных appearance-потоков аннотаций,
// без проверки покрытия листа изображением и без установления происхождения скана.
// Отмена проверяется между объектами/страницами и при чтении; внутренний вызов
// PDFium может завершиться позднее. Исходный PDF не изменяется.
Result CountRasterPages(HANDLE file, int expected_pages, const std::atomic_bool& cancel);

// Собирает новый документ в исходном порядке: растровые страницы импортируются
// из source, остальные — последовательно из converted. Повторного JPEG-сжатия
// исходных растровых изображений нет. output — новый пустой файл для записи.
// Закладки, метаданные и интерактивность документа в новом PDF не переносятся.
// Все дескрипторы заимствованы; исходники не меняются, частичный output удаляет caller.
DWORD CombinePages(HANDLE source, HANDLE converted, HANDLE output,
    const std::vector<unsigned char>& page_is_raster, const std::atomic_bool& cancel);
}
