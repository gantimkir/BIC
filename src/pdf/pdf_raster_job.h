#pragma once

#include "pdf_file_info.h"

#include <atomic>
#include <memory>

namespace PdfRasterJob
{
enum class Phase { Snapshot, Analyze, Render, Combine, Validate, Ready, Save, Saved };

// Worker не использует HWND. UI читает обычные поля только после done(acquire),
// phase — атомарно в любое время. Последняя shared_ptr удаляет временные файлы.
// Повторное сохранение использует тот же готовый PDF, без новой растеризации.
struct Job
{
    Pdf::FileInfo source;
    std::wstring directory;
    std::wstring saved_path;
    std::uint64_t before_bytes = 0;
    std::uint64_t after_bytes = 0;
    DWORD error = ERROR_SUCCESS;
    // Исходная ошибка Ghostscript и его ограниченный журнал сохраняются для
    // диагностики. Нормальные поля доступны UI только после done(acquire).
    HRESULT engine_status = S_OK;
    int engine_api_code = 0;
    int engine_exit_code = 0;
    std::string engine_diagnostics;
    bool ready = false;
    bool save_to_desktop = false;
    HANDLE thread = nullptr;
    std::atomic_bool cancel{false};
    std::atomic_bool done{false};
    std::atomic<Phase> phase{Phase::Snapshot};
    ~Job();
};

// Запускает подготовку 300 DPI / RGB JPEG 80% на отдельном потоке.
// При ошибке запуска выбрасывает исключение, которое обрабатывает UI.
std::shared_ptr<Job> Start(const Pdf::FileInfo& file);
// Вызывать только после done и ready; false — поток сохранения не создан.
bool Save(const std::shared_ptr<Job>& job, bool desktop);
// Кооперативная отмена без ожидания завершения потока, безопасна для UI.
void Cancel(const std::shared_ptr<Job>& job) noexcept;
// Имя результата, всегда с суффиксом _rastre перед .pdf.
std::wstring OutputName(const Pdf::FileInfo& file);
}
