#pragma once

#include <windows.h>
#include <string>

namespace OfficeDocument
{
enum class Application { Excel, Word, PowerPoint };
enum class Placement { Keep, LeftHalf, RightHalf, Maximized, Rectangle };

struct WindowOptions
{
    Placement placement = Placement::Keep;
    RECT rectangle{}; // Экранные координаты в пикселях; используется для Rectangle.
    HMONITOR monitor = nullptr; // Для LeftHalf/RightHalf; nullptr — основной монитор.
    bool activate = true; // Запросить передний план; Office сам также может активировать окно.
};

struct OpenOptions
{
    std::wstring path;
    Application application = Application::Excel;
    WindowOptions window{};
    bool read_only = false;
    bool reuse_application = true;
};

enum class Stage { Validate, InitializeCom, Connect, Security, Open, Window, Complete };
struct OpenResult
{
    HRESULT status = E_FAIL;
    Stage stage = Stage::Validate;
    bool document_opened = false; // Документ доступен: найден ранее или открыт этим вызовом.
    bool reused_document = false; // Книга Excel уже была открыта; повторного Open не было.
    HRESULT security_restore_status = S_OK;
    explicit operator bool() const noexcept
    {
        return SUCCEEDED(status) && SUCCEEDED(security_restore_status);
    }
};

// Синхронный вызов в STA-потоке. Office остаётся открыт после возврата.
// Не показывает диалоги BIC; собственные диалоги Office возможны.
OpenResult Open(const OpenOptions& options);
std::wstring DescribeError(const OpenResult& result);
}
