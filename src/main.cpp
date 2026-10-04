#include <windows.h>

#include "single_instance.h"
#include "resource.h"
#include "main_window.h"


int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    // НАЧАЛО: запуск только одного экземпляра приложения.
    // Объект удерживает mutex до выхода из wWinMain.
    const SingleInstance single_instance(MainWindow::kClassName, MainWindow::kTitle);
    if (single_instance.GetResult() == SingleInstance::Result::Error)
    {
        return 1;
    }
    if (single_instance.GetResult() == SingleInstance::Result::AlreadyRunning)
    {
        // Завершаем второй процесс, не создавая нового окна.
        return 0;
    }
    // КОНЕЦ: запуск только одного экземпляра приложения.

    const WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = MainWindow::WindowProcedure,
        .hInstance = instance,
        // Иконка окна, панели задач и переключателя Alt+Tab.
        .hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_BIC),
            IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED)),
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = MainWindow::kClassName,
        .hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_BIC),
            IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED)),
    };

    if (RegisterClassExW(&window_class) == 0)
    {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        MainWindow::kClassName,
        MainWindow::kTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        900,
        600,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window == nullptr)
    {
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
