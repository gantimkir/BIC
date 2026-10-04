#pragma once

#include <windows.h>

class SingleInstance
{
public:
    enum class Result
    {
        Primary,
        AlreadyRunning,
        Error,
    };

    SingleInstance(const wchar_t* window_class_name, const wchar_t* window_title);
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&&) = delete;
    SingleInstance& operator=(SingleInstance&&) = delete;

    Result GetResult() const noexcept { return result_; }

private:
    static void ActivateExistingWindow(const wchar_t* window_class_name);

    HANDLE mutex_ = nullptr;
    Result result_ = Result::Error;
};
