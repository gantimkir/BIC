#include "explorer_folder.h"

#include <exdisp.h>
#include <shldisp.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <vector>

namespace
{
using Microsoft::WRL::ComPtr;
struct Pending
{
    HWND owner;
    std::wstring path;
    RECT bounds; // Снимок общей области из рамки «Размещение» при открытии.
    ULONGLONG deadline;
};
// Доступ только из UI-потока: таймер не блокирует окно ожиданием запуска Explorer.
std::vector<Pending> pending;
UINT_PTR timer = 0;

void Error(HWND owner, const std::wstring& message)
{
    MessageBoxW(owner, message.c_str(), L"Папки", MB_OK | MB_ICONERROR);
}

// Сравниваем путь документа Shell, а не заголовок/активное окно или PID запуска:
// Explorer может передать открытие существующему процессу или вкладке.
HWND FindFolder(const std::wstring& path)
{
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER,
            IID_PPV_ARGS(windows.GetAddressOf())))) return nullptr;
    long count = 0;
    if (FAILED(windows->get_Count(&count))) return nullptr;
    for (long i = 0; i < count; ++i)
    {
        VARIANT index{};
        index.vt = VT_I4;
        index.lVal = i;
        ComPtr<IDispatch> dispatch;
        ComPtr<IWebBrowser2> browser;
        ComPtr<IDispatch> document;
        ComPtr<IShellFolderViewDual> view;
        ComPtr<Folder> folder;
        ComPtr<Folder2> folder2;
        ComPtr<FolderItem> item;
        if (FAILED(windows->Item(index, &dispatch)) || !dispatch ||
            FAILED(dispatch.As(&browser)) ||
            FAILED(browser->get_Document(&document)) || !document ||
            FAILED(document.As(&view)) || FAILED(view->get_Folder(&folder)) || !folder ||
            FAILED(folder.As(&folder2)) || FAILED(folder2->get_Self(&item)) || !item) continue;
        BSTR actual = nullptr;
        const HRESULT result = item->get_Path(&actual);
        const bool matches = SUCCEEDED(result) && actual != nullptr &&
            CompareStringOrdinal(actual, -1, path.c_str(), -1, TRUE) == CSTR_EQUAL;
        SysFreeString(actual);
        if (!matches) continue;
        SHANDLE_PTR handle = 0;
        if (SUCCEEDED(browser->get_HWND(&handle)))
        {
            const HWND window = reinterpret_cast<HWND>(handle);
            if (IsWindow(window)) return GetAncestor(window, GA_ROOT);
        }
    }
    return nullptr;
}

void CALLBACK OnTimer(HWND, UINT, UINT_PTR, DWORD)
{
    // COM-ссылки освобождаются внутри FindFolder до балансирующего Uninitialize.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<Pending> failed;
    for (size_t i = 0; i < pending.size();)
    {
        const HWND window = SUCCEEDED(initialized) ? FindFolder(pending[i].path) : nullptr;
        bool done = false;
        bool success = false;
        if (window != nullptr)
        {
            // Область уже вычислена общим обработчиком кнопок до открытия папки.
            // Ожидание Explorer и смена переключателя не меняют этот снимок.
            const RECT target = pending[i].bounds;
            ShowWindow(window, SW_RESTORE);
            success = SetWindowPos(window, HWND_TOP, target.left, target.top,
                target.right - target.left, target.bottom - target.top,
                SWP_SHOWWINDOW) != FALSE;
            if (success) SetForegroundWindow(window);
            done = true;
        }
        else if (FAILED(initialized) || GetTickCount64() >= pending[i].deadline)
        {
            done = true;
        }
        if (!done) { ++i; continue; }
        if (!success) failed.push_back(pending[i]);
        pending.erase(pending.begin() + i);
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (pending.empty() && timer != 0)
    {
        KillTimer(nullptr, timer);
        timer = 0;
    }
    // Модальные сообщения допускают повторный вход; очередь уже приведена в порядок.
    for (const auto& request : failed)
        Error(request.owner, L"Не удалось найти или разместить окно Проводника для папки:\n" +
            request.path + L"\nПапка могла открыться без изменения положения окна.");
}
}

void ExplorerFolder::Open(HWND owner, const std::wstring& path, const RECT& bounds)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        Error(owner, L"Папка не существует или недоступна:\n" + path);
        return;
    }
    // Передаём папку оболочке как объект, без конструирования командной строки.
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_FLAG_NO_UI;
    execute.hwnd = owner;
    execute.lpVerb = L"explore";
    execute.lpFile = path.c_str();
    execute.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&execute))
    {
        Error(owner, L"Не удалось открыть папку (код " + std::to_wstring(GetLastError()) + L"):\n" + path);
        return;
    }
    // Повтор той же папки обновляет срок и положение по последнему запросу.
    for (auto& request : pending)
    {
        if (CompareStringOrdinal(request.path.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            request.deadline = GetTickCount64() + 15000;
            request.bounds = bounds;
            return;
        }
    }
    pending.push_back({owner, path, bounds, GetTickCount64() + 15000});
    if (timer == 0) timer = SetTimer(nullptr, 0, 200, OnTimer);
    if (timer == 0)
    {
        pending.clear();
        Error(owner, L"Папка открыта, но не удалось запустить ожидание окна Проводника.");
    }
}

void ExplorerFolder::Cancel()
{
    if (timer != 0) KillTimer(nullptr, timer);
    timer = 0;
    pending.clear();
}
