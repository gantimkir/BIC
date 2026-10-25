#include "pdf_report_dialog.h"

#include "pdf_raster_job.h"
#include "resource.h"

#include <cwchar>
#include <new>
#include <string>
#include <utility>

namespace
{
constexpr UINT_PTR kRasterTimer = 1;
constexpr int kCompactHeight = 134;
constexpr int kExpandedHeight = 291;
constexpr int kBlockControls[]{IDC_PDF_RASTER_SEPARATOR, IDC_PDF_RASTER_SETTINGS,
    IDC_PDF_RASTER_STATUS, IDC_PDF_RASTER_BEFORE, IDC_PDF_RASTER_AFTER,
    IDC_PDF_RASTER_DELTA, IDC_PDF_RASTER_NAME, IDC_PDF_RASTER_FOLDER,
    IDC_PDF_RASTER_DESKTOP, IDC_PDF_RASTER_SAVE, IDC_PDF_RASTER_DISCARD};

struct DialogContext
{
    const std::vector<Pdf::FileInfo>& files;
    size_t current_index = 0;
    // Сохранённая копия заменяет представление соответствующего файла только
    // в этом сеансе просмотра. Запись переживает сворачивание блока и навигацию;
    // исходный список вызывающего кода и файлы на диске не изменяются.
    std::vector<std::optional<Pdf::FileInfo>> saved_files;
    std::shared_ptr<PdfRasterJob::Job> job;
    bool expanded = false;
    bool completion_handled = false;
};

void ShowError(HWND owner, const wchar_t* action, DWORD error) noexcept
{
    wchar_t message[512]{};
    swprintf_s(message, L"%s\nКод ошибки: %lu", action, error);
    MessageBoxW(owner, message, L"PDF", MB_OK | MB_ICONERROR);
}

bool Busy(const DialogContext& context) noexcept
{
    return context.job && !context.job->done.load(std::memory_order_acquire);
}

bool IsSaved(const DialogContext& context) noexcept
{
    return context.current_index < context.saved_files.size() &&
        context.saved_files[context.current_index].has_value();
}

const Pdf::FileInfo& CurrentFile(const DialogContext& context) noexcept
{
    return IsSaved(context) ? *context.saved_files[context.current_index] :
        context.files[context.current_index];
}

Pdf::FileInfo SavedFileInfo(const PdfRasterJob::Job& job)
{
    // Сохранялся именно проверенный result.pdf, без повторного преобразования:
    // его размер и число страниц известны из подготовки и успешной публикации.
    // На UI-потоке не запускаем дополнительное чтение или разбор PDF.
    Pdf::FileInfo file;
    file.path = job.saved_path;
    file.size_bytes = job.after_bytes;
    file.page_count = job.source.page_count;
    file.raster_page_count = job.source.page_count;
    file.raster_pages.assign(static_cast<size_t>(*file.page_count), 1);
    const auto name = file.path.substr(file.path.find_last_of(L"\\/") + 1);
    file.display_text = L"Имя: " + name + L"\r\nПуть: " + file.path +
        L"\r\nРазмер: " + std::to_wstring(*file.size_bytes) + L" байт" +
        L"\r\nСтраниц: " + std::to_wstring(*file.page_count) +
        L"\r\nРастеризованных страниц: " + std::to_wstring(*file.raster_page_count);
    return file;
}

bool CanRasterize(const Pdf::FileInfo& file) noexcept
{
    // Неизвестный результат не равен отсутствию растровых страниц: без полной
    // классификации выборочное преобразование нельзя выполнять корректно.
    return file.page_count && file.raster_page_count && file.source_identity &&
        *file.page_count > 0 && *file.raster_page_count >= 0 &&
        *file.raster_page_count < *file.page_count &&
        file.raster_pages.size() == static_cast<size_t>(*file.page_count);
}

void UpdateButtons(HWND dialog, const DialogContext& context) noexcept
{
    const bool busy = Busy(context);
    const bool saved = IsSaved(context);
    EnableWindow(GetDlgItem(dialog, IDC_PDF_PREVIOUS), !busy && context.current_index > 0);
    EnableWindow(GetDlgItem(dialog, IDC_PDF_NEXT), !busy && context.current_index + 1 < context.files.size());
    EnableWindow(GetDlgItem(dialog, IDC_PDF_RASTERIZE),
        !busy && !saved && CanRasterize(CurrentFile(context)));
    // Обычные поля job читаются лишь после done(acquire); короткое замыкание
    // здесь обязательно, пока worker меняет ready/error/saved_path.
    const bool ready = !busy && context.job && context.job->ready;
    EnableWindow(GetDlgItem(dialog, IDC_PDF_RASTER_SAVE),
        ready && !saved && context.job->phase.load() != PdfRasterJob::Phase::Saved);
    EnableWindow(GetDlgItem(dialog, IDC_PDF_RASTER_FOLDER), !busy && !saved);
    EnableWindow(GetDlgItem(dialog, IDC_PDF_RASTER_DESKTOP), !busy && !saved);
    SetDlgItemTextW(dialog, IDC_PDF_RASTER_DISCARD, busy ? L"Отмена" : L"Свернуть");
}

bool SetExpanded(HWND dialog, DialogContext& context, bool expanded) noexcept
{
    // MapDialogRect учитывает шрифт диалога. Изменяем высоту клиентской области,
    // сохраняя ширину и положение; весь нижний блок скрыт в компактном режиме.
    RECT target{0, 0, 340, expanded ? kExpandedHeight : kCompactHeight};
    if (!MapDialogRect(dialog, &target)) { return false; }
    if (!AdjustWindowRectEx(&target, static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_STYLE)),
            FALSE, static_cast<DWORD>(GetWindowLongPtrW(dialog, GWL_EXSTYLE)))) { return false; }
    RECT current{};
    if (!GetWindowRect(dialog, &current)) { return false; }
    // При раскрытии поднимаем окно, если нижний блок вышел бы за рабочую область
    // текущего монитора. Во время работы диалог не пересоздаётся.
    const int height = target.bottom - target.top;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    int top = current.top;
    if (GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor))
    {
        if (top + height > monitor.rcWork.bottom) { top = monitor.rcWork.bottom - height; }
        if (top < monitor.rcWork.top) { top = monitor.rcWork.top; }
    }
    if (!SetWindowPos(dialog, nullptr, current.left, top, current.right - current.left,
            height, SWP_NOZORDER | SWP_NOACTIVATE)) { return false; }
    for (const int id : kBlockControls) { ShowWindow(GetDlgItem(dialog, id), expanded ? SW_SHOW : SW_HIDE); }
    context.expanded = expanded;
    return true;
}

void ResetBlock(HWND dialog, DialogContext& context)
{
    // Навигация разрешена только после done: завершённый job безопасно удалить,
    // вместе с ним удаляются его временные source/converted/result PDF.
    context.job.reset();
    context.completion_handled = false;
    if (!SetExpanded(dialog, context, false))
    {
        ShowError(dialog, L"Не удалось уменьшить окно сведений PDF.", GetLastError());
    }
}

bool ShowCurrentFile(HWND dialog, DialogContext& context)
{
    const auto& files = context.files;
    if (context.current_index >= files.size()) { return false; }
    wchar_t position[64]{};
    swprintf_s(position, L"Файл %zu из %zu", context.current_index + 1, files.size());
    if (!SetDlgItemTextW(dialog, IDC_PDF_POSITION, position) ||
        !SetDlgItemTextW(dialog, IDC_PDF_DETAILS_TEXT, CurrentFile(context).display_text.c_str()))
    {
        ShowError(dialog, L"Не удалось показать сведения о PDF.", GetLastError());
        EndDialog(dialog, IDCANCEL);
        return false;
    }
    UpdateButtons(dialog, context);
    return true;
}

void ShowSize(HWND dialog, int control, const wchar_t* label, std::uint64_t bytes) noexcept
{
    wchar_t text[160]{};
    swprintf_s(text, L"%s: %llu байт (%.2f МиБ)", label,
        static_cast<unsigned long long>(bytes), static_cast<double>(bytes) / (1024.0 * 1024.0));
    SetDlgItemTextW(dialog, control, text);
}

const wchar_t* PhaseText(PdfRasterJob::Phase phase) noexcept
{
    switch (phase)
    {
    case PdfRasterJob::Phase::Snapshot: return L"Создание снимка исходного файла…";
    case PdfRasterJob::Phase::Analyze: return L"Проверка страниц исходного файла…";
    case PdfRasterJob::Phase::Render: return L"Растеризация нерастровых страниц…";
    case PdfRasterJob::Phase::Combine: return L"Сборка PDF в исходном порядке страниц…";
    case PdfRasterJob::Phase::Validate: return L"Проверка готового PDF…";
    case PdfRasterJob::Phase::Save: return L"Сохранение выбранного результата…";
    default: return L"Подготовка…";
    }
}

std::wstring FailureDetails(const PdfRasterJob::Job& job, const wchar_t* message)
{
    std::wstring text = message;
    text += L"\r\nЭтап: ";
    text += PhaseText(job.phase.load());
    text += L"\r\nКод Windows: " + std::to_wstring(job.error);
    if (FAILED(job.engine_status))
    {
        wchar_t codes[160]{};
        swprintf_s(codes, L"\r\nGhostscript: HRESULT 0x%08lX, API %d, exit %d",
            static_cast<unsigned long>(job.engine_status), job.engine_api_code, job.engine_exit_code);
        text += codes;
        // Вывод движка хранится как байты. Для сообщения декодируем ограниченный
        // фрагмент UTF-8 с заменой невалидных последовательностей, без выполнения
        // или интерпретации текста. Полный журнал не раздувает MessageBox.
        const int bytes = static_cast<int>(job.engine_diagnostics.size() > 4096
            ? 4096 : job.engine_diagnostics.size());
        const int length = MultiByteToWideChar(CP_UTF8, 0,
            job.engine_diagnostics.data(), bytes, nullptr, 0);
        if (length > 0)
        {
            std::wstring diagnostic(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, job.engine_diagnostics.data(), bytes,
                diagnostic.data(), length);
            text += L"\r\n\r\n" + diagnostic;
        }
    }
    return text;
}

void RefreshJob(HWND dialog, DialogContext& context)
{
    if (!context.job) { return; }
    const auto& job = context.job;
    if (!job->done.load(std::memory_order_acquire))
    {
        SetDlgItemTextW(dialog, IDC_PDF_RASTER_STATUS,
            job->cancel.load() ? L"Отмена операции…" : PhaseText(job->phase.load()));
        return;
    }
    if (context.completion_handled) { return; }
    // Обработчик MessageBox может повторно войти в модальный цикл таймера.
    context.completion_handled = true;
    if (job->error == ERROR_SUCCESS && job->phase.load() == PdfRasterJob::Phase::Saved)
    {
        // Только успешная публикация меняет текущую запись. При ошибке/отмене
        // сведения исходника и возможность повторного сохранения сохраняются.
        context.saved_files[context.current_index] = SavedFileInfo(*job);
        if (!ShowCurrentFile(dialog, context)) { return; }
        // Кнопка «Сохранить» стала недоступной: фокус переносится на видимую
        // доступную команду, а не остаётся на отключённом контроле.
        SetFocus(GetDlgItem(dialog, IDC_PDF_RASTER_DISCARD));
    }
    UpdateButtons(dialog, context);
    if (job->ready)
    {
        ShowSize(dialog, IDC_PDF_RASTER_BEFORE, L"До", job->before_bytes);
        ShowSize(dialog, IDC_PDF_RASTER_AFTER, L"После", job->after_bytes);
        const double delta = job->before_bytes == 0 ? 0.0 :
            (static_cast<double>(job->after_bytes) / static_cast<double>(job->before_bytes) - 1.0) * 100.0;
        wchar_t text[160]{};
        swprintf_s(text, L"Изменение: %+.2f%%%s", delta,
            job->after_bytes > job->before_bytes ? L" — файл стал больше" : L"");
        SetDlgItemTextW(dialog, IDC_PDF_RASTER_DELTA, text);
    }
    if (job->error != ERROR_SUCCESS)
    {
        const wchar_t* message = L"Не удалось подготовить или сохранить PDF.";
        if (job->error == ERROR_CANCELLED) { message = L"Операция отменена."; }
        else if (job->error == ERROR_FILE_INVALID) { message = L"Исходный файл изменился. Выберите PDF заново."; }
        else if (job->error == ERROR_FILE_EXISTS || job->error == ERROR_ALREADY_EXISTS)
        {
            message = L"Файл с именем _rastre.pdf уже существует. Выберите другую папку.";
        }
        SetDlgItemTextW(dialog, IDC_PDF_RASTER_STATUS, message);
        if (job->error != ERROR_CANCELLED)
        {
            const auto details = FailureDetails(*job, message);
            MessageBoxW(dialog, details.c_str(), L"PDF", MB_OK | MB_ICONERROR);
        }
    }
    else if (job->phase.load() == PdfRasterJob::Phase::Saved)
    {
        const std::wstring message = L"Сохранено: " + job->saved_path;
        SetDlgItemTextW(dialog, IDC_PDF_RASTER_STATUS, message.c_str());
    }
    else
    {
        SetDlgItemTextW(dialog, IDC_PDF_RASTER_STATUS, L"Результат готов. Сравните размеры и выберите место сохранения.");
    }
}

void StartRaster(HWND dialog, DialogContext& context)
{
    if (Busy(context) || IsSaved(context) || !CanRasterize(CurrentFile(context))) { return; }
    context.job.reset();
    context.completion_handled = false;
    CheckRadioButton(dialog, IDC_PDF_RASTER_FOLDER, IDC_PDF_RASTER_DESKTOP, IDC_PDF_RASTER_FOLDER);
    SetDlgItemTextW(dialog, IDC_PDF_RASTER_BEFORE, L"До: —");
    SetDlgItemTextW(dialog, IDC_PDF_RASTER_AFTER, L"После: —");
    SetDlgItemTextW(dialog, IDC_PDF_RASTER_DELTA, L"Изменение: —");
    const auto name = L"Имя: " + PdfRasterJob::OutputName(context.files[context.current_index]);
    SetDlgItemTextW(dialog, IDC_PDF_RASTER_NAME, name.c_str());
    if (!SetExpanded(dialog, context, true))
    {
        ShowError(dialog, L"Не удалось раскрыть блок растеризации.", GetLastError());
        return;
    }
    context.job = PdfRasterJob::Start(context.files[context.current_index]);
    UpdateButtons(dialog, context);
    SetFocus(GetDlgItem(dialog, IDC_PDF_RASTER_DISCARD));
    RefreshJob(dialog, context);
}

INT_PTR DialogProcedureImpl(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param)
{
    auto* context = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        context = reinterpret_cast<DialogContext*>(l_param);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
        // Новые обязательные ID обнаруживают старый app.rc.res до запуска worker.
        for (const int id : kBlockControls)
        {
            if (GetDlgItem(dialog, id) == nullptr)
            {
                ShowError(dialog, L"Ресурс окна PDF устарел. Пересоберите BIC.", ERROR_CONTROL_ID_NOT_FOUND);
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
        }
        for (const int id : {IDC_PDF_POSITION, IDC_PDF_DETAILS_TEXT, IDC_PDF_PREVIOUS, IDC_PDF_RASTERIZE, IDC_PDF_NEXT})
        {
            if (GetDlgItem(dialog, id) == nullptr)
            {
                ShowError(dialog, L"Ресурс окна PDF устарел. Пересоберите BIC.", ERROR_CONTROL_ID_NOT_FOUND);
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
        }
        context->saved_files.resize(context->files.size());
        if (context->files.empty() || !SetExpanded(dialog, *context, false) ||
            !ShowCurrentFile(dialog, *context))
        {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        if (SetTimer(dialog, kRasterTimer, 100, nullptr) == 0)
        {
            ShowError(dialog, L"Не удалось включить обновление растеризации.", GetLastError());
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        SetFocus(GetDlgItem(dialog, IDCANCEL));
        return FALSE;
    }
    if (message == WM_TIMER && w_param == kRasterTimer && context)
    {
        RefreshJob(dialog, *context);
        return TRUE;
    }
    if (message == WM_COMMAND && context)
    {
        // Завершение может случиться между тиками таймера и нажатием «Свернуть».
        // Сначала фиксируем успешное сохранение, затем разрешаем сбросить job.
        RefreshJob(dialog, *context);
        const int command = LOWORD(w_param);
        if (command == IDC_PDF_RASTERIZE)
        {
            StartRaster(dialog, *context);
            return TRUE;
        }
        if (command == IDC_PDF_RASTER_SAVE)
        {
            if (!Busy(*context) && !IsSaved(*context) && context->job && context->job->ready)
            {
                context->completion_handled = false;
                PdfRasterJob::Save(context->job,
                    IsDlgButtonChecked(dialog, IDC_PDF_RASTER_DESKTOP) == BST_CHECKED);
                UpdateButtons(dialog, *context);
                RefreshJob(dialog, *context);
            }
            return TRUE;
        }
        if (command == IDC_PDF_RASTER_DISCARD)
        {
            if (Busy(*context))
            {
                PdfRasterJob::Cancel(context->job);
                RefreshJob(dialog, *context);
            }
            else
            {
                ResetBlock(dialog, *context);
                UpdateButtons(dialog, *context);
                SetFocus(GetDlgItem(dialog, IsSaved(*context) ? IDCANCEL : IDC_PDF_RASTERIZE));
            }
            return TRUE;
        }
        if (command == IDC_PDF_PREVIOUS || command == IDC_PDF_NEXT)
        {
            if (Busy(*context)) { return TRUE; }
            if (command == IDC_PDF_PREVIOUS && context->current_index > 0) { --context->current_index; }
            else if (command == IDC_PDF_NEXT && context->current_index + 1 < context->files.size()) { ++context->current_index; }
            ResetBlock(dialog, *context);
            ShowCurrentFile(dialog, *context);
            return TRUE;
        }
    }
    if ((message == WM_COMMAND && (LOWORD(w_param) == IDOK || LOWORD(w_param) == IDCANCEL)) || message == WM_CLOSE)
    {
        if (context) { PdfRasterJob::Cancel(context->job); }
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (message == WM_DESTROY)
    {
        if (context) { PdfRasterJob::Cancel(context->job); }
        KillTimer(dialog, kRasterTimer);
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        return TRUE;
    }
    return FALSE;
}

// Аллокации форматирования и запуска не должны выводить исключения за WNDPROC.
INT_PTR CALLBACK DialogProcedure(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) noexcept
{
    try { return DialogProcedureImpl(dialog, message, w_param, l_param); }
    catch (...)
    {
        auto* context = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (context) { PdfRasterJob::Cancel(context->job); }
        ShowError(dialog, L"Не удалось выполнить операцию PDF.", ERROR_NOT_ENOUGH_MEMORY);
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
}
} // namespace

void PdfReportDialog::Show(HWND owner, HINSTANCE instance, const std::vector<Pdf::FileInfo>& files) noexcept
{
    DialogContext context{files};
    const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_PDF_DETAILS), owner,
        DialogProcedure, reinterpret_cast<LPARAM>(&context));
    const DWORD dialog_error = result == -1 ? GetLastError() : ERROR_SUCCESS;
    PdfRasterJob::Cancel(context.job);
    if (result == -1) { ShowError(owner, L"Не удалось показать сведения о PDF.", dialog_error); }
}
