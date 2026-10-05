#include "contract_registry.h"

#include <oleauto.h>

namespace
{
constexpr wchar_t kRegistryPath[] =
    L"C:\\YD\\ИП Гантимуров КВ\\Договоры\\Реестр договоров.xlsx";

HRESULT Invoke(IDispatch* object, const wchar_t* name, WORD flags,
    VARIANT* result, VARIANT* arguments = nullptr, UINT argument_count = 0)
{
    auto* member_name = const_cast<wchar_t*>(name);
    DISPID member_id{};
    HRESULT status = object->GetIDsOfNames(IID_NULL, &member_name, 1,
        LOCALE_USER_DEFAULT, &member_id);
    if (FAILED(status))
    {
        return status;
    }

    DISPID property_put = DISPID_PROPERTYPUT;
    DISPPARAMS parameters{arguments, nullptr, argument_count, 0};
    if (flags == DISPATCH_PROPERTYPUT)
    {
        parameters.rgdispidNamedArgs = &property_put;
        parameters.cNamedArgs = 1;
    }
    return object->Invoke(member_id, IID_NULL, LOCALE_USER_DEFAULT,
        flags, &parameters, result, nullptr, nullptr);
}

bool OpenInExcel()
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
    {
        return false;
    }

    CLSID excel_class{};
    IDispatch* excel = nullptr;
    IUnknown* active = nullptr;
    VARIANT workbooks{};
    VARIANT workbook{};
    VARIANT window_handle{};
    VARIANT visible{};
    VARIANT path{};
    VARIANT previous_security{};
    bool security_changed = false;
    bool success = false;

    if (SUCCEEDED(CLSIDFromProgID(L"Excel.Application", &excel_class)))
    {
        if (SUCCEEDED(GetActiveObject(excel_class, nullptr, &active)))
        {
            active->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&excel));
            active->Release();
        }
        if (excel != nullptr || SUCCEEDED(CoCreateInstance(excel_class, nullptr,
            CLSCTX_LOCAL_SERVER, IID_IDispatch, reinterpret_cast<void**>(&excel))))
        {
            visible.vt = VT_BOOL;
            visible.boolVal = VARIANT_TRUE;
            path.vt = VT_BSTR;
            path.bstrVal = SysAllocString(kRegistryPath);
            VARIANT security_by_ui{};
            security_by_ui.vt = VT_I4;
            security_by_ui.lVal = 2; // msoAutomationSecurityByUI

            if (SUCCEEDED(Invoke(excel, L"AutomationSecurity", DISPATCH_PROPERTYGET,
                &previous_security)) &&
                SUCCEEDED(Invoke(excel, L"AutomationSecurity", DISPATCH_PROPERTYPUT,
                    nullptr, &security_by_ui)))
            {
                security_changed = true;
            }

            if (security_changed && path.bstrVal != nullptr &&
                SUCCEEDED(Invoke(excel, L"Visible", DISPATCH_PROPERTYPUT, nullptr, &visible)) &&
                SUCCEEDED(Invoke(excel, L"Workbooks", DISPATCH_PROPERTYGET, &workbooks)) &&
                workbooks.vt == VT_DISPATCH &&
                SUCCEEDED(Invoke(workbooks.pdispVal, L"Open", DISPATCH_METHOD,
                    &workbook, &path, 1)))
            {
                if (workbook.vt == VT_DISPATCH)
                {
                    Invoke(workbook.pdispVal, L"Activate", DISPATCH_METHOD, nullptr);
                }
                if (SUCCEEDED(Invoke(excel, L"Hwnd", DISPATCH_PROPERTYGET, &window_handle)) &&
                    window_handle.vt == VT_I4)
                {
                    HWND excel_window = reinterpret_cast<HWND>(
                        static_cast<UINT_PTR>(static_cast<DWORD>(window_handle.lVal)));
                    RECT work_area{};
                    if (IsWindow(excel_window) &&
                        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0))
                    {
                        ShowWindow(excel_window, SW_RESTORE);
                        success = SetWindowPos(excel_window, nullptr,
                            work_area.left, work_area.top,
                            (work_area.right - work_area.left) / 2,
                            work_area.bottom - work_area.top,
                            SWP_NOZORDER) != FALSE;
                        if (success)
                        {
                            SetForegroundWindow(excel_window);
                        }
                    }
                }
            }
        }
    }

    if (security_changed)
    {
        Invoke(excel, L"AutomationSecurity", DISPATCH_PROPERTYPUT,
            nullptr, &previous_security);
    }
    VariantClear(&path);
    VariantClear(&workbooks);
    VariantClear(&workbook);
    VariantClear(&window_handle);
    VariantClear(&previous_security);
    if (excel != nullptr)
    {
        excel->Release();
    }
    CoUninitialize();
    return success;
}
} // namespace

void ContractRegistry::Open(HWND owner)
{
    if (GetFileAttributesW(kRegistryPath) == INVALID_FILE_ATTRIBUTES)
    {
        MessageBoxW(owner, L"Файл реестра договоров не найден.",
            L"Реестр договоров", MB_OK | MB_ICONERROR);
        return;
    }
    if (!OpenInExcel())
    {
        MessageBoxW(owner, L"Не удалось открыть или разместить реестр договоров в Excel.",
            L"Реестр договоров", MB_OK | MB_ICONERROR);
    }
}
