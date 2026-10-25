#pragma once

#include <windows.h>

#include "pdf_file_info.h"

#include <vector>

namespace PdfReportDialog
{
// Показывает один PDF за раз после завершения всего рабочего задания.
// Вызывающий код сохраняет готовый вектор до закрытия модального окна.
// Раскрываемый блок растеризации владеет своим shared job; при закрытии окна
// отмена запрашивается без ожидания, worker не обращается к уничтоженным HWND.
void Show(HWND owner, HINSTANCE instance, const std::vector<Pdf::FileInfo>& files) noexcept;
}
