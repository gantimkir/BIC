#include "land_plans_editor.h"
#include "land_plans_config.h"
#include "land_plans_menu.h"
#include "land_plans_editor_resource.h"
#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>

namespace
{
struct Context
{
    LandPlansConfig::Snapshot snapshot;
    std::vector<LandPlansConfig::Entry> entries;
    int selected = -1;
    bool dirty = false; // Изменения списка, ещё не записанные на диск.
    bool draft = false; // Изменения поля, ещё не применённые к строке списка.
    bool filling = false;
};

void Error(HWND dialog, const std::wstring& text)
{
    MessageBoxW(dialog, text.c_str(), L"Редактор списка", MB_OK | MB_ICONWARNING);
}

std::wstring Field(HWND dialog)
{
    HWND edit = GetDlgItem(dialog, IDC_LP_VALUE);
    const int size = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<size_t>(size) + 1, L'\0');
    GetWindowTextW(edit, text.data(), size + 1);
    text.resize(size);
    return text;
}

void UpdateControls(HWND dialog, const Context& context)
{
    const bool selected = context.selected >= 0 && static_cast<size_t>(context.selected) < context.entries.size();
    EnableWindow(GetDlgItem(dialog, IDC_LP_UPDATE), selected);
    EnableWindow(GetDlgItem(dialog, IDC_LP_DELETE), selected);
    EnableWindow(GetDlgItem(dialog, IDC_LP_UP), selected && context.selected > 0);
    EnableWindow(GetDlgItem(dialog, IDC_LP_DOWN), selected && static_cast<size_t>(context.selected + 1) < context.entries.size());
    EnableWindow(GetDlgItem(dialog, IDC_LP_BROWSE),
        SendDlgItemMessageW(dialog, IDC_LP_TYPE, CB_GETCURSEL, 0, 0) == 0);
    InvalidateRect(GetDlgItem(dialog, IDC_LP_PREVIEW), nullptr, TRUE);
}

void FillFields(HWND dialog, Context& context)
{
    context.filling = true;
    const bool selected = context.selected >= 0 && static_cast<size_t>(context.selected) < context.entries.size();
    SendDlgItemMessageW(dialog, IDC_LP_TYPE, CB_SETCURSEL,
        selected && context.entries[context.selected].separator ? 1 : 0, 0);
    SetDlgItemTextW(dialog, IDC_LP_VALUE, selected ? context.entries[context.selected].value.c_str() : L"");
    context.filling = false;
    context.draft = false;
    UpdateControls(dialog, context);
}

bool Refresh(HWND dialog, Context& context)
{
    // Редактор показывает порядок файла, чтобы границы групп можно было перемещать.
    // Автоматическая сортировка применяется только при показе меню.
    HWND list = GetDlgItem(dialog, IDC_LP_LIST);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    HDC dc = GetDC(list);
    HGDIOBJ previous = nullptr;
    if (dc) previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0)));
    int width = 0;
    bool success = true;
    for (const auto& entry : context.entries)
    {
        const std::wstring text = entry.separator ? L"[Разделитель] " + entry.value : entry.value;
        const LRESULT result = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        if (result == LB_ERR || result == LB_ERRSPACE) { success = false; break; }
        SIZE size{};
        if (dc && GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size))
            width = (std::max)(width, static_cast<int>(size.cx) + 8);
    }
    if (dc) { SelectObject(dc, previous); ReleaseDC(list, dc); }
    if (!success)
    {
        Error(dialog, L"Недостаточно ресурсов для отображения списка. Изменения не сохранены.");
        EndDialog(dialog, IDCANCEL);
        return false;
    }
    SendMessageW(list, LB_SETHORIZONTALEXTENT, width, 0);
    SendMessageW(list, LB_SETCURSEL, context.selected, 0);
    FillFields(dialog, context);
    return true;
}

bool DiscardDraft(HWND dialog, const Context& context)
{
    return !context.draft || MessageBoxW(dialog,
        L"Не применённые изменения поля будут потеряны. Продолжить?",
        L"Редактор списка", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

void Browse(HWND dialog)
{
    // Диалог выбора папки — STA COM; все ссылки освобождаются до CoUninitialize.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) { Error(dialog, L"Не удалось открыть выбор папки."); return; }
    HRESULT result = E_FAIL;
    {
        Microsoft::WRL::ComPtr<IFileDialog> picker;
        result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&picker));
        if (SUCCEEDED(result))
        {
            DWORD options = 0;
            result = picker->GetOptions(&options);
            if (SUCCEEDED(result)) result = picker->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            if (SUCCEEDED(result)) result = picker->Show(dialog);
            Microsoft::WRL::ComPtr<IShellItem> item;
            if (SUCCEEDED(result)) result = picker->GetResult(&item);
            PWSTR path = nullptr;
            if (SUCCEEDED(result)) result = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(result) && path) SetDlgItemTextW(dialog, IDC_LP_VALUE, path);
            CoTaskMemFree(path);
        }
    }
    CoUninitialize();
    if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        Error(dialog, L"Не удалось выбрать папку (HRESULT " + std::to_wstring(result) + L").");
}

INT_PTR CALLBACK Procedure(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* context = reinterpret_cast<Context*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        context = reinterpret_cast<Context*>(lparam);
        SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        SendDlgItemMessageW(dialog, IDC_LP_TYPE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Папка"));
        SendDlgItemMessageW(dialog, IDC_LP_TYPE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Разделитель"));
        SendDlgItemMessageW(dialog, IDC_LP_VALUE, EM_SETLIMITTEXT, 32767, 0);
        context->selected = context->entries.empty() ? -1 : 0;
        Refresh(dialog, *context);
        return TRUE;
    }
    if (!context) return FALSE;
    if (message == WM_DRAWITEM)
    {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (item && item->CtlID == IDC_LP_PREVIEW)
        {
            FillRect(item->hDC, &item->rcItem, GetSysColorBrush(COLOR_BTNFACE));
            if (SendDlgItemMessageW(dialog, IDC_LP_TYPE, CB_GETCURSEL, 0, 0) == 1)
                LandPlansMenu::DrawSeparator(item->hDC, item->rcItem, Field(dialog),
                    reinterpret_cast<HFONT>(SendMessageW(dialog, WM_GETFONT, 0, 0)));
            return TRUE;
        }
    }
    if (message == WM_CLOSE)
    {
        SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        return TRUE;
    }
    if (message != WM_COMMAND) return FALSE;
    const int id = LOWORD(wparam);
    const int notification = HIWORD(wparam);
    if ((id == IDC_LP_VALUE && notification == EN_CHANGE) ||
        (id == IDC_LP_TYPE && notification == CBN_SELCHANGE))
    {
        if (!context->filling) context->draft = true;
        UpdateControls(dialog, *context);
        return TRUE;
    }
    if (id == IDC_LP_LIST && notification == LBN_SELCHANGE)
    {
        if (!DiscardDraft(dialog, *context))
            SendDlgItemMessageW(dialog, IDC_LP_LIST, LB_SETCURSEL, context->selected, 0);
        else
        {
            context->selected = static_cast<int>(SendDlgItemMessageW(dialog, IDC_LP_LIST, LB_GETCURSEL, 0, 0));
            FillFields(dialog, *context);
        }
        return TRUE;
    }
    if (notification != BN_CLICKED) return FALSE;
    if (id == IDCANCEL)
    {
        if ((!context->dirty && !context->draft) || MessageBoxW(dialog,
            L"Закрыть редактор без сохранения изменений?", L"Редактор списка",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    if (id == IDOK)
    {
        if (context->draft)
        {
            Error(dialog, L"Сначала нажмите «Добавить» или «Применить к строке» для изменений в поле.");
            return TRUE;
        }
        std::wstring error;
        if (!LandPlansConfig::Save(context->snapshot, context->entries, error)) Error(dialog, error);
        else EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (id == IDC_LP_BROWSE) { Browse(dialog); return TRUE; }
    if (id == IDC_LP_ADD || id == IDC_LP_UPDATE)
    {
        LandPlansConfig::Entry entry{
            SendDlgItemMessageW(dialog, IDC_LP_TYPE, CB_GETCURSEL, 0, 0) == 1, Field(dialog)};
        std::wstring error;
        if (!LandPlansConfig::Normalize(entry, error)) { Error(dialog, error); return TRUE; }
        if (id == IDC_LP_ADD)
        {
            // Новая запись вставляется после выделенной, в том числе после заголовка группы.
            const size_t at = context->selected < 0 ? context->entries.size() : static_cast<size_t>(context->selected + 1);
            context->entries.insert(context->entries.begin() + at, entry);
            context->selected = static_cast<int>(at);
        }
        else if (context->selected >= 0) context->entries[context->selected] = entry;
        else return TRUE;
        context->dirty = true;
        Refresh(dialog, *context);
        return TRUE;
    }
    if (id == IDC_LP_DELETE || id == IDC_LP_UP || id == IDC_LP_DOWN)
    {
        if (context->selected < 0 || !DiscardDraft(dialog, *context)) return TRUE;
        const size_t selected = static_cast<size_t>(context->selected);
        if (id == IDC_LP_DELETE)
        {
            context->entries.erase(context->entries.begin() + selected);
            context->selected = context->entries.empty() ? -1 :
                (std::min)(context->selected, static_cast<int>(context->entries.size() - 1));
        }
        else
        {
            const int target = context->selected + (id == IDC_LP_UP ? -1 : 1);
            if (target < 0 || static_cast<size_t>(target) >= context->entries.size()) return TRUE;
            std::swap(context->entries[selected], context->entries[target]);
            context->selected = target;
        }
        context->dirty = true;
        Refresh(dialog, *context);
        return TRUE;
    }
    return FALSE;
}
}

void LandPlansEditor::Show(HWND owner)
{
    Context context;
    std::wstring error;
    if (!LandPlansConfig::Load(context.snapshot, error)) { Error(owner, error); return; }
    context.entries = context.snapshot.entries;
    const auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_LAND_PLANS_EDITOR), owner,
            Procedure, reinterpret_cast<LPARAM>(&context)) == -1)
        Error(owner, L"Не удалось открыть редактор списка (код " + std::to_wstring(GetLastError()) + L").");
}
