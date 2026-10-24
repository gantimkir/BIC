#pragma once

#include <windows.h>

#include "pdf_file_info.h"

#include <vector>

namespace PdfReportDialog
{
// Показывает один PDF за раз после завершения всего рабочего задания.
// Вызывающий код сохраняет готовый вектор до закрытия модального окна.
void Show(HWND owner, HINSTANCE instance, const std::vector<Pdf::FileInfo>& files) noexcept;
}
