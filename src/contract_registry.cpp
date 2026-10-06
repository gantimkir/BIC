#include "contract_registry.h"

#include "office_document.h"

// Команда задаёт только файл и параметры; COM и размещение окна — в общем модуле.
void ContractRegistry::Open(HWND owner)
{
    const OfficeDocument::OpenOptions options{
        .path = L"C:\\YD\\ИП Гантимуров КВ\\Договоры\\Реестр договоров.xlsx",
        .application = OfficeDocument::Application::Excel,
        .window = {.placement = OfficeDocument::Placement::LeftHalf},
    };
    const auto result = OfficeDocument::Open(options);
    if (!result)
    {
        const auto message = OfficeDocument::DescribeError(result);
        MessageBoxW(owner, message.c_str(), L"Реестр договоров", MB_OK | MB_ICONERROR);
    }
}
