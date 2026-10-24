#include "pdf_report_dialog.h"

#include "resource.h"

#include <cwchar>

namespace
{
struct DialogContext
{
    const std::vector<Pdf::FileInfo>& files;
    size_t current_index = 0;
};

void ShowError(HWND owner, const wchar_t* action, DWORD error) noexcept
{
    wchar_t message[512]{};
    swprintf_s(message, L"%s\nКод ошибки: %lu", action, error);
    MessageBoxW(owner, message, L"PDF", MB_OK | MB_ICONERROR);
}

// Переключение использует только готовые строки после завершения worker.
// На границах списка и при одном файле соответствующие кнопки недоступны.
bool ShowCurrentFile(HWND dialog, DialogContext* context) noexcept
{
    const auto& files = context->files;
    if (context->current_index >= files.size())
    {
        return false;
    }
    wchar_t position[64]{};
    swprintf_s(position, L"Файл %zu из %zu", context->current_index + 1, files.size());
    if (!SetDlgItemTextW(dialog, IDC_PDF_POSITION, position) ||
        !SetDlgItemTextW(dialog, IDC_PDF_DETAILS_TEXT,
            files[context->current_index].display_text.c_str()))
    {
        ShowError(dialog, L"Не удалось показать сведения о PDF.", GetLastError());
        EndDialog(dialog, IDCANCEL);
        return false;
    }
    EnableWindow(GetDlgItem(dialog, IDC_PDF_PREVIOUS), context->current_index > 0);
    EnableWindow(GetDlgItem(dialog, IDC_PDF_NEXT), context->current_index + 1 < files.size());
    return true;
}

INT_PTR CALLBACK DialogProcedure(HWND dialog, UINT message, WPARAM w_param, LPARAM l_param) noexcept
{
    auto* context = reinterpret_cast<DialogContext*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        context = reinterpret_cast<DialogContext*>(l_param);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(context));
        // В старом app.rc.res есть поле текста, но нет счётчика и навигации.
        // Сообщаем о несовпадении сразу, чтобы не выдавать код 1421 после чтения PDF.
        if (GetDlgItem(dialog, IDC_PDF_POSITION) == nullptr ||
            GetDlgItem(dialog, IDC_PDF_DETAILS_TEXT) == nullptr ||
            GetDlgItem(dialog, IDC_PDF_PREVIOUS) == nullptr ||
            GetDlgItem(dialog, IDC_PDF_NEXT) == nullptr)
        {
            ShowError(dialog, L"Ресурс окна PDF устарел. Пересоберите BIC.",
                ERROR_CONTROL_ID_NOT_FOUND);
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        // Окно создаётся только после успешного завершения worker: первый
        // файл можно показывать сразу, без таймера и промежуточного текста.
        if (!ShowCurrentFile(dialog, context)) { return TRUE; }
        SetFocus(GetDlgItem(dialog, IDCANCEL));
        return FALSE;
    }
    if (message == WM_COMMAND && context != nullptr &&
        (LOWORD(w_param) == IDC_PDF_PREVIOUS || LOWORD(w_param) == IDC_PDF_NEXT))
    {
        if (LOWORD(w_param) == IDC_PDF_PREVIOUS && context->current_index > 0)
        {
            --context->current_index;
        }
        else if (LOWORD(w_param) == IDC_PDF_NEXT &&
            context->current_index + 1 < context->files.size())
        {
            ++context->current_index;
        }
        ShowCurrentFile(dialog, context);
        return TRUE;
    }
    if ((message == WM_COMMAND && (LOWORD(w_param) == IDOK || LOWORD(w_param) == IDCANCEL)) ||
        message == WM_CLOSE)
    {
        EndDialog(dialog, message == WM_CLOSE ? IDCANCEL : LOWORD(w_param));
        return TRUE;
    }
    if (message == WM_DESTROY)
    {
        SetWindowLongPtrW(dialog, DWLP_USER, 0);
        return TRUE;
    }
    return FALSE;
}
} // namespace

void PdfReportDialog::Show(HWND owner, HINSTANCE instance,
    const std::vector<Pdf::FileInfo>& files) noexcept
{
    DialogContext context{files};
    const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_PDF_DETAILS), owner,
        DialogProcedure, reinterpret_cast<LPARAM>(&context));
    const DWORD dialog_error = result == -1 ? GetLastError() : ERROR_SUCCESS;
    if (result == -1)
    {
        ShowError(owner, L"Не удалось показать сведения о PDF.", dialog_error);
    }
}
