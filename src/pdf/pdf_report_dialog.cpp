#include "pdf_report_dialog.h"

#include "resource.h"

#include <cwchar>

namespace
{
constexpr UINT_PTR kMetadataTimer = 1;
constexpr ULONGLONG kMetadataTimeoutMs = 30000;

struct DialogContext
{
    const PdfReportDialog::State& state;
    ULONGLONG started = GetTickCount64();
};

void ShowError(HWND owner, const wchar_t* action, DWORD error) noexcept
{
    wchar_t message[512]{};
    swprintf_s(message, L"%s\nКод ошибки: %lu", action, error);
    MessageBoxW(owner, message, L"PDF", MB_OK | MB_ICONERROR);
}

void Cancel(const DialogContext* context) noexcept
{
    if (context != nullptr && !context->state.done.load(std::memory_order_acquire))
    {
        context->state.cancel.store(true);
        // CancelSynchronousIo лишь запрашивает отмену текущего ввода-вывода.
        // Владелец данных сохраняет их до завершения рабочего потока.
        CancelSynchronousIo(context->state.worker_thread);
    }
}

INT_PTR CALLBACK DialogProcedure(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) noexcept
{
    auto* context = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        context = reinterpret_cast<DialogContext*>(l_param);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
        SetDlgItemTextW(dialog, IDC_FILE_SIZES_TEXT, L"Получение размеров и числа страниц PDF…");
        EnableWindow(GetDlgItem(dialog, IDOK), FALSE);
        if (SetTimer(dialog, kMetadataTimer, 100, nullptr) == 0)
        {
            const DWORD error = GetLastError();
            Cancel(context);
            ShowError(dialog, L"Не удалось включить ожидание результата.", error);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        SetFocus(GetDlgItem(dialog, IDCANCEL));
        return FALSE;
    }
    if (message == WM_TIMER && w_param == kMetadataTimer && context != nullptr)
    {
        const bool done = context->state.done.load(std::memory_order_acquire);
        if (done || GetTickCount64() - context->started >= kMetadataTimeoutMs)
        {
            KillTimer(dialog, kMetadataTimer);
            const wchar_t* text = nullptr;
            if (!done)
            {
                Cancel(context);
                text = L"Время ожидания превысило 30 секунд. Операция отменена.";
            }
            else if (context->state.error == ERROR_NOT_ENOUGH_MEMORY)
            {
                text = L"Недостаточно памяти для списка файлов. Выберите меньше файлов.";
            }
            else if (context->state.error == ERROR_NETWORK_ACCESS_DENIED)
            {
                text = L"Выберите файлы из одной локальной папки. Сетевые расположения не принимаются.";
            }
            else if (context->state.error != ERROR_SUCCESS)
            {
                text = L"Получение сведений о PDF отменено или список превышает допустимый объём.";
            }
            else
            {
                text = context->state.report.c_str();
            }
            if (!SetDlgItemTextW(dialog, IDC_FILE_SIZES_TEXT, text))
            {
                ShowError(dialog, L"Не удалось показать список файлов.", GetLastError());
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }
            EnableWindow(GetDlgItem(dialog, IDOK), TRUE);
            ShowWindow(GetDlgItem(dialog, IDCANCEL), SW_HIDE);
            SetFocus(GetDlgItem(dialog, IDOK));
        }
        return TRUE;
    }
    if ((message == WM_COMMAND && (LOWORD(w_param) == IDOK || LOWORD(w_param) == IDCANCEL)) ||
        message == WM_CLOSE)
    {
        Cancel(context);
        EndDialog(dialog, message == WM_CLOSE ? IDCANCEL : LOWORD(w_param));
        return TRUE;
    }
    if (message == WM_DESTROY)
    {
        KillTimer(dialog, kMetadataTimer);
        Cancel(context);
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        return TRUE;
    }
    return FALSE;
}
} // namespace

void PdfReportDialog::Show(HWND owner, HINSTANCE instance, const State& state) noexcept
{
    DialogContext context{state};
    const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_FILE_SIZES), owner,
        DialogProcedure, reinterpret_cast<LPARAM>(&context));
    const DWORD dialog_error = result == -1 ? GetLastError() : ERROR_SUCCESS;
    Cancel(&context);
    if (result == -1)
    {
        ShowError(owner, L"Не удалось показать сведения о PDF.", dialog_error);
    }
}
