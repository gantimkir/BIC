#include <windows.h>

namespace
{
constexpr wchar_t kWindowClassName[] = L"BIC.MainWindow";
constexpr wchar_t kWindowTitle[] = L"Business information center";

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message)
    {
    case WM_CREATE:
    {
        HWND button = CreateWindowExW(
            0, L"BUTTON", L"Закрыть",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 100, 32, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)),
            reinterpret_cast<LPCREATESTRUCTW>(l_param)->hInstance, nullptr);
        if (button == nullptr)
        {
            return -1;
        }
        SendMessageW(button, WM_SETFONT,
            reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return 0;
    }
    case WM_SIZE:
    {
        constexpr int margin = 16;
        constexpr int width = 100;
        constexpr int height = 32;
        RECT client{};
        GetClientRect(window, &client);
        MoveWindow(GetDlgItem(window, 1),
            client.right > width + margin ? client.right - width - margin : 0,
            client.bottom > height + margin ? client.bottom - height - margin : 0,
            width, height, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w_param) == 1 && HIWORD(w_param) == BN_CLICKED &&
            reinterpret_cast<HWND>(l_param) == GetDlgItem(window, 1))
        {
            SendMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        return DefWindowProcW(window, message, w_param, l_param);
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    const WNDCLASSEXW window_class{
        .cbSize = sizeof(WNDCLASSEXW),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WindowProcedure,
        .hInstance = instance,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = kWindowClassName,
    };

    if (RegisterClassExW(&window_class) == 0)
    {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClassName,
        kWindowTitle,
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
