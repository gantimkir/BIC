#include "pzz_61_41_graphics.h"

#include <shlobj.h>

#include <string>

namespace
{
constexpr wchar_t kImagePath[] =
    L"C:\\YD\\Межевые планы\\ПЗЗ\\Цимлянск\\ТЗ 61-41.jpg";
constexpr wchar_t kWindowTitle[] = L"ПЗЗ 61-41. Графика";

struct WindowSearch
{
    DWORD process_id = 0;
    HWND window = nullptr;
};

// EnumWindows вызывает функцию для всех верхнеуровневых окон. Нас интересует
// видимое основное окно созданного rundll32, а не служебное или дочернее окно.
BOOL CALLBACK FindProcessWindow(HWND window, LPARAM parameter)
{
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD process_id = 0;
    GetWindowThreadProcessId(window, &process_id);
    if (process_id == search->process_id && IsWindowVisible(window) &&
        GetWindow(window, GW_OWNER) == nullptr)
    {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HWND WaitForProcessWindow(HANDLE process, DWORD process_id)
{
    // Большой JPG загружается не мгновенно, поэтому после готовности очереди
    // сообщений даём просмотрщику до 15 секунд на создание видимого окна.
    WaitForInputIdle(process, 5000);
    for (int attempt = 0; attempt < 150; ++attempt)
    {
        WindowSearch search{.process_id = process_id};
        EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
        if (search.window != nullptr)
        {
            return search.window;
        }
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0)
        {
            break;
        }
        Sleep(100);
    }
    return nullptr;
}

bool IsRegularFile(const wchar_t* path)
{
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void ShowError(HWND owner, const std::wstring& message)
{
    MessageBoxW(owner, message.c_str(), kWindowTitle, MB_OK | MB_ICONERROR);
}
} // namespace

void Pzz6141Graphics::Open(HWND owner)
{
    if (!IsRegularFile(kImagePath))
    {
        ShowError(owner, L"Файл изображения не найден:\n" + std::wstring(kImagePath));
        return;
    }

    PWSTR program_files_buffer = nullptr;
    const HRESULT folder_result = SHGetKnownFolderPath(
        FOLDERID_ProgramFiles, KF_FLAG_DEFAULT, nullptr, &program_files_buffer);
    if (FAILED(folder_result) || program_files_buffer == nullptr)
    {
        ShowError(owner, L"Не удалось определить папку Program Files.");
        return;
    }

    const std::wstring program_files(program_files_buffer);
    CoTaskMemFree(program_files_buffer);
    const std::wstring viewer_dll =
        program_files + L"\\Windows Photo Viewer\\PhotoViewer.dll";
    if (!IsRegularFile(viewer_dll.c_str()))
    {
        ShowError(owner, L"Средство просмотра фотографий Windows не найдено:\n" + viewer_dll);
        return;
    }

    wchar_t system_directory[MAX_PATH]{};
    if (GetSystemDirectoryW(system_directory, MAX_PATH) == 0)
    {
        ShowError(owner, L"Не удалось определить системную папку Windows.");
        return;
    }

    const std::wstring rundll32 = std::wstring(system_directory) + L"\\rundll32.exe";
    // Синтаксис соответствует зарегистрированной в Windows команде Photo Viewer.
    // CreateProcessW изменяет буфер командной строки, поэтому используется data().
    // ImageView_Fullscreen получает остаток строки как единый путь. Кавычки
    // вокруг изображения не снимаются и становятся частью имени файла, поэтому
    // здесь они намеренно отсутствуют даже при пробелах в пути.
    std::wstring command_line = L"\"" + rundll32 + L"\" \"" + viewer_dll +
        L"\",ImageView_Fullscreen " + kImagePath;

    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &startup_info, &process_info))
    {
        const DWORD error = GetLastError();
        ShowError(owner, L"Не удалось запустить средство просмотра фотографий Windows.\n"
            L"Код ошибки: " + std::to_wstring(error));
        return;
    }

    CloseHandle(process_info.hThread);
    HWND viewer_window = WaitForProcessWindow(process_info.hProcess, process_info.dwProcessId);
    if (viewer_window == nullptr)
    {
        CloseHandle(process_info.hProcess);
        ShowError(owner, L"Изображение передано средству просмотра, но его окно не найдено.");
        return;
    }

    RECT work_area{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0))
    {
        const DWORD error = GetLastError();
        CloseHandle(process_info.hProcess);
        ShowError(owner, L"Не удалось определить рабочую область экрана.\n"
            L"Код ошибки: " + std::to_wstring(error));
        return;
    }

    const int work_width = work_area.right - work_area.left;
    const int work_height = work_area.bottom - work_area.top;
    const int left = work_area.left + work_width / 2;
    const int width = work_area.right - left;
    const int height = work_height / 2;

    ShowWindow(viewer_window, SW_RESTORE);
    if (!SetWindowPos(viewer_window, nullptr, left, work_area.top, width, height,
            SWP_NOZORDER | SWP_SHOWWINDOW))
    {
        const DWORD error = GetLastError();
        CloseHandle(process_info.hProcess);
        ShowError(owner, L"Изображение открыто, но окно не удалось разместить.\n"
            L"Код ошибки: " + std::to_wstring(error));
        return;
    }

    SetForegroundWindow(viewer_window);
    CloseHandle(process_info.hProcess);
}
