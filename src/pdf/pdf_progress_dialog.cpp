#include "pdf_progress_dialog.h"

#include "resource.h"

#include <cwchar>

namespace
{
constexpr UINT_PTR kProgressTimer = 1;
constexpr UINT kRefreshMs = 50;
constexpr ULONGLONG kMinFileDisplayMs = 500;

struct DialogContext
{
    const PdfProgressDialog::State& state;
    size_t shown = 0;
    ULONGLONG last_step = 0;
};

void ShowError(HWND owner, const wchar_t* action, DWORD error) noexcept
{
    wchar_t message[512]{};
    swprintf_s(message, L"%s\nКод ошибки: %lu", action, error);
    MessageBoxW(owner, message, L"PDF", MB_OK | MB_ICONERROR);
}

void RequestCancel(const DialogContext* context) noexcept
{
    if (context != nullptr && !context->state.done.load(std::memory_order_acquire))
    {
        context->state.cancel.store(true);
        // Прерывание синхронного ввода-вывода лишь запрашивается. Worker
        // продолжает владеть данными и завершает Ghostscript самостоятельно.
        CancelSynchronousIo(context->state.worker_thread);
    }
}

bool ShowCount(HWND dialog, const DialogContext& context) noexcept
{
    wchar_t message[128]{};
    swprintf_s(message, L"Обработано PDF: %zu из %zu", context.shown, context.state.total);
    return SetDlgItemTextW(dialog, IDC_PDF_PROGRESS_TEXT, message) != 0;
}

void ShowWorkerFailure(HWND owner, DWORD error) noexcept
{
    const wchar_t* message = nullptr;
    if (error == ERROR_NOT_ENOUGH_MEMORY)
    {
        message = L"Недостаточно памяти для сведений о PDF. Выберите меньше файлов.";
    }
    else if (error == ERROR_NETWORK_ACCESS_DENIED)
    {
        message = L"Выберите файлы из одной локальной папки. Сетевые расположения не принимаются.";
    }
    else if (error == ERROR_SUCCESS)
    {
        message = L"Не удалось получить сведения обо всех выбранных PDF.";
    }
    else
    {
        message = L"Получение сведений о PDF отменено или данные превышают допустимый объём.";
    }
    MessageBoxW(owner, message, L"PDF", MB_OK | MB_ICONERROR);
}

INT_PTR CALLBACK DialogProcedure(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) noexcept
{
    auto* context = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        context = reinterpret_cast<DialogContext*>(l_param);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
        if (GetDlgItem(dialog, IDC_PDF_PROGRESS_TEXT) == nullptr)
        {
            RequestCancel(context);
            ShowError(dialog, L"Ресурс окна загрузки PDF устарел. Пересоберите BIC.",
                ERROR_CONTROL_ID_NOT_FOUND);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        if (!ShowCount(dialog, *context))
        {
            const DWORD error = GetLastError();
            RequestCancel(context);
            ShowError(dialog, L"Не удалось показать прогресс PDF.", error);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        if (SetTimer(dialog, kProgressTimer, kRefreshMs, nullptr) == 0)
        {
            const DWORD error = GetLastError();
            RequestCancel(context);
            ShowError(dialog, L"Не удалось включить обновление прогресса PDF.", error);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        SetFocus(GetDlgItem(dialog, IDCANCEL));
        return FALSE;
    }
    if (message == WM_TIMER && w_param == kProgressTimer && context != nullptr)
    {
        const bool done = context->state.done.load(std::memory_order_acquire);
        const size_t processed = context->state.processed.load(std::memory_order_acquire);
        if (done && (context->state.error != ERROR_SUCCESS || processed != context->state.total))
        {
            EndDialog(dialog, IDABORT);
            return TRUE;
        }
        const ULONGLONG now = GetTickCount64();
        // Worker может обогнать интерфейс. Показываем завершённые файлы по одному:
        // каждый шаг занимает минимум 500 мс, но медленное чтение не замедляем.
        if (processed > context->shown &&
            (context->shown == 0 || now - context->last_step >= kMinFileDisplayMs))
        {
            ++context->shown;
            context->last_step = now;
            if (!ShowCount(dialog, *context))
            {
                const DWORD error = GetLastError();
                RequestCancel(context);
                ShowError(dialog, L"Не удалось показать прогресс PDF.", error);
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
        }
        // Последнее значение тоже остаётся на экране полсекунды до отчёта.
        if (done && context->shown == context->state.total &&
            now - context->last_step >= kMinFileDisplayMs)
        {
            EndDialog(dialog, IDOK);
        }
        return TRUE;
    }
    if ((message == WM_COMMAND && LOWORD(w_param) == IDCANCEL) || message == WM_CLOSE)
    {
        RequestCancel(context);
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (message == WM_DESTROY)
    {
        KillTimer(dialog, kProgressTimer);
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        return TRUE;
    }
    return FALSE;
}
} // namespace

bool PdfProgressDialog::Show(HWND owner, HINSTANCE instance, const State& state) noexcept
{
    DialogContext context{state};
    const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_PDF_PROGRESS),
        owner, DialogProcedure, reinterpret_cast<LPARAM>(&context));
    const DWORD dialog_error = result == -1 ? GetLastError() : ERROR_SUCCESS;
    if (result == IDOK)
    {
        return true;
    }
    RequestCancel(&context);
    if (result == -1)
    {
        ShowError(owner, L"Не удалось показать загрузку PDF.", dialog_error);
    }
    else if (result == IDABORT)
    {
        // error доступен после публикации done; при ошибке логики счётчика он
        // может остаться ERROR_SUCCESS, что обрабатывается отдельным текстом.
        ShowWorkerFailure(owner, state.error);
    }
    return false;
}
