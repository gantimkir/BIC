#include "single_instance.h"

// НАЧАЛО: запуск только одного экземпляра приложения.
SingleInstance::SingleInstance(const wchar_t* window_class_name, const wchar_t* window_title)
{
    // Именованный mutex общий для всех процессов BIC в текущем сеансе Windows.
    mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\BIC.SingleInstance");
    if (mutex_ == nullptr)
    {
        MessageBoxW(nullptr, L"Не удалось проверить, запущено ли приложение.",
            window_title, MB_OK | MB_ICONERROR);
        return;
    }

    // Сохраняем результат сразу: другие вызовы Windows могут изменить GetLastError().
    const DWORD mutex_error = GetLastError();
    if (mutex_error == ERROR_ALREADY_EXISTS)
    {
        ActivateExistingWindow(window_class_name);
        result_ = Result::AlreadyRunning;
        return;
    }

    result_ = Result::Primary;
}

SingleInstance::~SingleInstance()
{
    // Дескриптор остаётся открытым до выхода из wWinMain, включая ранние return.
    // После завершения последнего процесса Windows удаляет этот mutex.
    if (mutex_ != nullptr)
    {
        CloseHandle(mutex_);
    }
}
// КОНЕЦ: запуск только одного экземпляра приложения.

void SingleInstance::ActivateExistingWindow(const wchar_t* window_class_name)
{
    // НАЧАЛО: активация окна ранее запущенного экземпляра.
    // При одновременном запуске mutex может появиться раньше самого окна.
    // Ждём появления окна не более двух секунд, включая скрытое в трее.
    HWND existing_window = nullptr;
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        existing_window = FindWindowW(window_class_name, nullptr);
        if (existing_window != nullptr)
        {
            break;
        }
        existing_window = nullptr;
        Sleep(50);
    }

    if (existing_window != nullptr)
    {
        // Восстанавливаем свёрнутое окно, сохраняя размер остальных окон.
        ShowWindowAsync(existing_window,
            IsIconic(existing_window) ? SW_RESTORE : SW_SHOW);

        // Windows может ограничить перевод чужого окна на передний план.
        // Если активация запрещена, привлекаем внимание через панель задач.
        if (!SetForegroundWindow(existing_window))
        {
            FLASHWINFO flash_info{
                .cbSize = sizeof(FLASHWINFO),
                .hwnd = existing_window,
                .dwFlags = FLASHW_TRAY,
                .uCount = 3,
                .dwTimeout = 0,
            };
            FlashWindowEx(&flash_info);
        }
    }
    // КОНЕЦ: активация окна ранее запущенного экземпляра.
}
