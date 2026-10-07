#include "office_document.h"

#include <oleauto.h>
#include <objidl.h>
#include <utility>
#include <wrl/client.h>

namespace
{
using Microsoft::WRL::ComPtr;
using namespace OfficeDocument;

// COM и VARIANT освобождаются и при раннем выходе из функции.
struct ComScope
{
    HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComScope() { if (SUCCEEDED(status)) CoUninitialize(); }
};
struct Value
{
    VARIANT data{};
    Value() = default;
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    ~Value() { VariantClear(&data); }
    IDispatch* Object() const
    {
        return data.vt == VT_DISPATCH ? data.pdispVal : nullptr;
    }
};

HRESULT Invoke(IDispatch* object, const wchar_t* name, WORD flags,
    VARIANT* result = nullptr, VARIANT* arguments = nullptr, UINT count = 0)
{
    if (!object) return E_POINTER;
    auto* member = const_cast<wchar_t*>(name);
    DISPID id{};
    HRESULT status = object->GetIDsOfNames(IID_NULL, &member, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(status)) return status;
    DISPID put = DISPID_PROPERTYPUT;
    DISPPARAMS params{arguments, nullptr, count, 0};
    if (flags == DISPATCH_PROPERTYPUT)
    {
        params.rgdispidNamedArgs = &put;
        params.cNamedArgs = 1;
    }
    return object->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags,
        &params, result, nullptr, nullptr);
}

HRESULT SetNumber(IDispatch* object, const wchar_t* name, LONG number, VARTYPE type = VT_I4)
{
    VARIANT value{};
    value.vt = type;
    if (type == VT_BOOL) value.boolVal = number ? VARIANT_TRUE : VARIANT_FALSE;
    else value.lVal = number;
    return Invoke(object, name, DISPATCH_PROPERTYPUT, nullptr, &value, 1);
}

// Excel.FullName содержит абсолютный путь; относительный путь запроса приводим к нему.
HRESULT FullPath(const std::wstring& path, std::wstring& full_path)
{
    const DWORD size = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!size) return HRESULT_FROM_WIN32(GetLastError());
    std::wstring buffer(size, L'\0');
    const DWORD length = GetFullPathNameW(path.c_str(), size, buffer.data(), nullptr);
    if (!length) return HRESULT_FROM_WIN32(GetLastError());
    if (length >= size) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    buffer.resize(length);
    full_path = std::move(buffer);
    return S_OK;
}

// По файловому моникеру ищем книгу во всех экземплярах Excel, зарегистрированных в ROT.
// GetObject у ROT не запускает Excel и не открывает файл.
HRESULT FindRunningExcelWorkbook(const std::wstring& path, ComPtr<IDispatch>& workbook)
{
    ComPtr<IRunningObjectTable> running;
    HRESULT status = GetRunningObjectTable(0, running.GetAddressOf());
    if (FAILED(status)) return status;
    ComPtr<IMoniker> moniker;
    status = CreateFileMoniker(path.c_str(), moniker.GetAddressOf());
    if (FAILED(status)) return status;
    ComPtr<IUnknown> object;
    status = running->GetObject(moniker.Get(), object.GetAddressOf());
    if (status == MK_E_UNAVAILABLE) return S_FALSE;
    if (FAILED(status)) return status;
    return object.As(&workbook);
}

// Запасной поиск: книга может ещё не появиться в ROT, но уже быть в Workbooks.
// Здесь доступен только экземпляр Excel, возвращённый GetActiveObject.
HRESULT FindExcelWorkbook(IDispatch* application, const std::wstring& path,
    ComPtr<IDispatch>& workbook)
{
    Value books, count;
    HRESULT status = Invoke(application, L"Workbooks", DISPATCH_PROPERTYGET, &books.data);
    if (FAILED(status)) return status;
    status = Invoke(books.Object(), L"Count", DISPATCH_PROPERTYGET, &count.data);
    if (FAILED(status)) return status;
    if (count.data.vt != VT_I4) return DISP_E_TYPEMISMATCH;
    for (LONG number = 1; number <= count.data.lVal; ++number)
    {
        VARIANT index{};
        index.vt = VT_I4;
        index.lVal = number;
        Value item, name;
        status = Invoke(books.Object(), L"Item", DISPATCH_PROPERTYGET, &item.data, &index, 1);
        if (FAILED(status)) return status;
        status = Invoke(item.Object(), L"FullName", DISPATCH_PROPERTYGET, &name.data);
        if (FAILED(status)) return status;
        if (name.data.vt != VT_BSTR) return DISP_E_TYPEMISMATCH;
        if (CompareStringOrdinal(name.data.bstrVal, -1, path.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            workbook = item.Object();
            return S_OK;
        }
    }
    return S_FALSE;
}

// Безопасность макросов меняется лишь на время открытия нового документа.
struct SecurityScope
{
    IDispatch* application;
    Value previous;
    bool changed = false;
    HRESULT Restore()
    {
        if (!changed) return S_OK;
        changed = false;
        return Invoke(application, L"AutomationSecurity", DISPATCH_PROPERTYPUT,
            nullptr, &previous.data, 1);
    }
    ~SecurityScope() { Restore(); }
    HRESULT Enable()
    {
        HRESULT status = Invoke(application, L"AutomationSecurity", DISPATCH_PROPERTYGET,
            &previous.data);
        if (FAILED(status)) return status;
        status = SetNumber(application, L"AutomationSecurity", 2); // ByUI
        changed = SUCCEEDED(status);
        return status;
    }
};

HRESULT TargetBounds(const WindowOptions& options, RECT& bounds)
{
    bounds = options.rectangle;
    if (options.placement == Placement::Rectangle) return S_OK;

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    HMONITOR monitor = options.monitor;
    if (!monitor) monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(monitor, &info)) return HRESULT_FROM_WIN32(GetLastError());
    bounds = info.rcWork;
    const LONG middle = bounds.left + (bounds.right - bounds.left) / 2;
    if (options.placement == Placement::LeftHalf) bounds.right = middle;
    else bounds.left = middle;
    return S_OK;
}

HRESULT PlaceWindow(IDispatch* document, const WindowOptions& options)
{
    if (options.placement == Placement::Keep && !options.activate) return S_OK;
    Value windows, window, handle;
    HRESULT status = Invoke(document, L"Windows", DISPATCH_PROPERTYGET, &windows.data);
    if (FAILED(status)) return status;
    VARIANT index{};
    index.vt = VT_I4;
    index.lVal = 1;
    status = Invoke(windows.Object(), L"Item", DISPATCH_PROPERTYGET, &window.data, &index, 1);
    if (FAILED(status)) return status;
    // Берём окно именно открытого документа, а не случайное активное окно Office.
    status = Invoke(window.Object(), L"Hwnd", DISPATCH_PROPERTYGET, &handle.data);
    if (FAILED(status)) return status;
    if (handle.data.vt != VT_I4) return DISP_E_TYPEMISMATCH;
    HWND hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(static_cast<DWORD>(handle.data.lVal)));
    if (!IsWindow(hwnd)) return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);

    if (options.placement == Placement::Maximized)
        ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    else if (options.placement != Placement::Keep)
    {
        RECT bounds{};
        status = TargetBounds(options, bounds);
        if (FAILED(status)) return status;
        ShowWindow(hwnd, SW_RESTORE);
        if (!SetWindowPos(hwnd, nullptr, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOZORDER | (options.activate ? 0 : SWP_NOACTIVATE)))
            return HRESULT_FROM_WIN32(GetLastError());
    }
    if (options.activate)
    {
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd); // Windows может отклонить запрос активации.
    }
    return S_OK;
}

// Word задаёт координаты окна в пунктах, поэтому используем его преобразование пикселей.
HRESULT PixelsToWordPoints(IDispatch* application, LONG pixels, bool vertical, LONG& points)
{
    VARIANT args[2]{};
    args[0].vt = VT_BOOL;
    args[0].boolVal = vertical ? VARIANT_TRUE : VARIANT_FALSE;
    args[1].vt = VT_R4;
    args[1].fltVal = static_cast<float>(pixels);
    Value converted;
    HRESULT status = Invoke(application, L"PixelsToPoints", DISPATCH_METHOD,
        &converted.data, args, 2);
    if (FAILED(status)) return status;
    VARIANT integer{};
    status = VariantChangeType(&integer, &converted.data, 0, VT_I4);
    if (SUCCEEDED(status)) points = integer.lVal;
    VariantClear(&integer);
    return status;
}

HRESULT PlaceWordWindow(IDispatch* document, IDispatch* application,
    const WindowOptions& options)
{
    if (options.placement == Placement::Keep && !options.activate) return S_OK;

    // В Word состояние окна можно менять только после активации документа.
    HRESULT status = Invoke(document, L"Activate", DISPATCH_METHOD);
    if (FAILED(status)) return status;
    Value window;
    status = Invoke(document, L"ActiveWindow", DISPATCH_PROPERTYGET, &window.data);
    if (FAILED(status)) return status;
    status = Invoke(window.Object(), L"Activate", DISPATCH_METHOD);
    if (FAILED(status)) return status;

    if (options.placement == Placement::Maximized)
        return SetNumber(window.Object(), L"WindowState", 1); // wdWindowStateMaximize
    if (options.placement != Placement::Keep)
    {
        status = SetNumber(window.Object(), L"WindowState", 0); // wdWindowStateNormal
        if (FAILED(status)) return status;

        RECT bounds{};
        status = TargetBounds(options, bounds);
        if (FAILED(status)) return status;
        LONG left{}, top{}, width{}, height{};
        status = PixelsToWordPoints(application, bounds.left, false, left);
        if (FAILED(status)) return status;
        status = PixelsToWordPoints(application, bounds.top, true, top);
        if (FAILED(status)) return status;
        status = PixelsToWordPoints(application, bounds.right - bounds.left, false, width);
        if (FAILED(status)) return status;
        status = PixelsToWordPoints(application, bounds.bottom - bounds.top, true, height);
        if (FAILED(status)) return status;

        status = SetNumber(window.Object(), L"Width", width);
        if (FAILED(status)) return status;
        status = SetNumber(window.Object(), L"Height", height);
        if (FAILED(status)) return status;
        status = SetNumber(window.Object(), L"Left", left);
        if (FAILED(status)) return status;
        status = SetNumber(window.Object(), L"Top", top);
        if (FAILED(status)) return status;
    }
    return S_OK;
}
}

OfficeDocument::OpenResult OfficeDocument::Open(const OpenOptions& options)
{
    OpenResult result;
    const wchar_t* prog_id = nullptr;
    const wchar_t* collection_name = nullptr;
    switch (options.application)
    {
    case Application::Excel: prog_id = L"Excel.Application"; collection_name = L"Workbooks"; break;
    case Application::Word: prog_id = L"Word.Application"; collection_name = L"Documents"; break;
    case Application::PowerPoint: prog_id = L"PowerPoint.Application"; collection_name = L"Presentations"; break;
    default: result.status = E_INVALIDARG; return result;
    }
    if (options.path.empty() || options.path.find(L'\0') != std::wstring::npos ||
        (options.window.placement == Placement::Rectangle &&
            (options.window.rectangle.right <= options.window.rectangle.left ||
             options.window.rectangle.bottom <= options.window.rectangle.top)))
    {
        result.status = E_INVALIDARG;
        return result;
    }
    const DWORD attributes = GetFileAttributesW(options.path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        result.status = HRESULT_FROM_WIN32(GetLastError());
        return result;
    }
    if (attributes & FILE_ATTRIBUTE_DIRECTORY)
    {
        result.status = HRESULT_FROM_WIN32(ERROR_DIRECTORY);
        return result;
    }
    result.stage = Stage::InitializeCom;
    ComScope com;
    result.status = com.status;
    if (FAILED(result.status)) return result;

    result.stage = Stage::Connect;
    std::wstring full_path;
    if (options.application == Application::Excel && options.reuse_application)
    {
        result.status = FullPath(options.path, full_path);
        if (FAILED(result.status)) return result;
        ComPtr<IDispatch> running_workbook;
        // Проверяем книгу до подключения к Excel: GetActiveObject может вернуть
        // другой экземпляр, в котором этой книги нет.
        result.status = FindRunningExcelWorkbook(full_path, running_workbook);
        if (FAILED(result.status)) return result;
        if (running_workbook)
        {
            Value owner;
            result.status = Invoke(running_workbook.Get(), L"Application",
                DISPATCH_PROPERTYGET, &owner.data);
            if (FAILED(result.status)) return result;
            result.status = SetNumber(owner.Object(), L"Visible", -1, VT_BOOL);
            if (FAILED(result.status)) return result;
            // Существующий режим доступа сохраняем: повторный Open и
            // перенастройка AutomationSecurity здесь не нужны.
            result.document_opened = true;
            result.reused_document = true;
            result.stage = Stage::Window;
            result.status = PlaceWindow(running_workbook.Get(), options.window);
            if (SUCCEEDED(result.status)) result.stage = Stage::Complete;
            return result;
        }
    }
    CLSID clsid{};
    result.status = CLSIDFromProgID(prog_id, &clsid);
    if (FAILED(result.status)) return result;
    ComPtr<IDispatch> application;
    if (options.reuse_application)
    {
        ComPtr<IUnknown> active;
        if (SUCCEEDED(GetActiveObject(clsid, nullptr, active.GetAddressOf()))) active.As(&application);
    }
    ComPtr<IDispatch> running_workbook;
    if (options.application == Application::Excel && options.reuse_application && application)
    {
        // ROT может ещё не содержать моникер книги. Перед новым Open проверяем
        // Workbooks уже подключённого экземпляра Excel.
        result.status = FindExcelWorkbook(application.Get(), full_path, running_workbook);
        if (FAILED(result.status)) return result;
    }
    if (!application)
    {
        result.status = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER,
            IID_PPV_ARGS(application.GetAddressOf()));
        if (FAILED(result.status)) return result;
    }
    const auto boolean_type = static_cast<VARTYPE>(
        options.application == Application::PowerPoint ? VT_I4 : VT_BOOL);
    result.status = SetNumber(application.Get(), L"Visible", -1, boolean_type);
    if (FAILED(result.status)) return result;

    if (running_workbook)
    {
        result.document_opened = true;
        result.reused_document = true;
        result.stage = Stage::Window;
        result.status = PlaceWindow(running_workbook.Get(), options.window);
        if (SUCCEEDED(result.status)) result.stage = Stage::Complete;
        return result;
    }
    result.stage = Stage::Security;
    SecurityScope security{application.Get()};
    result.status = security.Enable();
    if (FAILED(result.status)) return result;

    result.stage = Stage::Open;
    Value collection, document, path;
    path.data.vt = VT_BSTR;
    path.data.bstrVal = SysAllocString(options.path.c_str());
    result.status = path.data.bstrVal ? S_OK : E_OUTOFMEMORY;
    if (SUCCEEDED(result.status))
        result.status = Invoke(application.Get(), collection_name, DISPATCH_PROPERTYGET, &collection.data);
    if (SUCCEEDED(result.status))
    {
        // IDispatch принимает позиционные аргументы в обратном порядке.
        VARIANT args[3]{};
        args[0].vt = boolean_type;
        if (args[0].vt == VT_I4) args[0].lVal = options.read_only ? -1 : 0;
        else args[0].boolVal = options.read_only ? VARIANT_TRUE : VARIANT_FALSE;
        UINT count = 2;
        if (options.application == Application::PowerPoint) args[1] = path.data;
        else
        {
            args[1].vt = VT_ERROR; // UpdateLinks (Excel) / ConfirmConversions (Word): по умолчанию.
            args[1].scode = DISP_E_PARAMNOTFOUND;
            args[2] = path.data;
            count = 3;
        }
        result.status = Invoke(collection.Object(), L"Open", DISPATCH_METHOD, &document.data, args, count);
        result.document_opened = SUCCEEDED(result.status);
    }
    result.security_restore_status = security.Restore();
    if (FAILED(result.status)) return result;
    result.stage = Stage::Window;
    result.status = options.application == Application::Word
        ? PlaceWordWindow(document.Object(), application.Get(), options.window)
        : PlaceWindow(document.Object(), options.window);
    if (SUCCEEDED(result.status)) result.stage = Stage::Complete;
    return result;
}

std::wstring OfficeDocument::DescribeError(const OpenResult& result)
{
    const wchar_t* stage = L"неизвестный этап";
    switch (result.stage)
    {
    case Stage::Validate: stage = L"проверка пути и параметров"; break;
    case Stage::InitializeCom: stage = L"инициализация COM"; break;
    case Stage::Connect: stage = L"подключение к Office"; break;
    case Stage::Security: stage = L"настройка безопасности Office"; break;
    case Stage::Open: stage = L"открытие документа"; break;
    case Stage::Window: stage = L"размещение окна"; break;
    case Stage::Complete: stage = L"завершение"; break;
    }
    wchar_t code[32]{};
    swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(result.status));
    std::wstring message = result.document_opened ? L"Документ открыт.\n" : L"Документ не открыт.\n";
    message += L"Этап: ";
    message += stage;
    message += L". Код: ";
    message += code;
    if (FAILED(result.security_restore_status))
    {
        swprintf_s(code, L"0x%08lX", static_cast<unsigned long>(result.security_restore_status));
        message += L"\nНе удалось восстановить AutomationSecurity: ";
        message += code;
    }
    return message;
}
