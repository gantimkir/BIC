#include "office_document.h"
#include <iostream>

int wmain()
{
    using namespace OfficeDocument;
    int failures = 0;
    const auto expect = [&](const OpenOptions& options, HRESULT expected, const char* name)
    {
        const auto result = Open(options);
        if (result || result.status != expected || result.stage != Stage::Validate || result.document_opened)
        {
            std::cerr << "Failed: " << name << '\n';
            ++failures;
        }
    };
    expect({}, E_INVALIDARG, "empty path");
    expect({.path = std::wstring(L"file\0.xlsx", 10)}, E_INVALIDARG, "embedded null");
    expect({.path = L".", .application = static_cast<Application>(99)}, E_INVALIDARG, "invalid application");
    expect({.path = L".", .window = {.placement = Placement::Rectangle}}, E_INVALIDARG, "empty rectangle");
    expect({.path = L".", .window = {.placement = Placement::Rectangle, .rectangle = {20, 20, 10, 10}}},
        E_INVALIDARG, "inverted rectangle");
    expect({.path = L"."}, HRESULT_FROM_WIN32(ERROR_DIRECTORY), "directory");
    expect({.path = L"BIC-missing-7a046a4b.xlsx"}, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "missing file");
    return failures ? 1 : 0;
}
