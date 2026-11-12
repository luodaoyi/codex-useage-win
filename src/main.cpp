#include "AppBarWindow.h"

#include <Windows.h>

#include <string>

namespace {

std::wstring FormatLastErrorMessage(DWORD error) {
    if (error == 0) {
        return L"unknown error";
    }

    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring message = L"Win32 error " + std::to_wstring(error);
    if (size != 0 && buffer != nullptr) {
        message += L": ";
        message += buffer;
        LocalFree(buffer);
    }
    return message;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR options, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    AppBarWindow window(instance);
    if (!window.Create(options)) {
        const std::wstring text = L"Failed to create CodexUsageBar window.\n\n" + FormatLastErrorMessage(GetLastError());
        MessageBoxW(nullptr, text.c_str(), L"CodexUsageBar", MB_ICONERROR | MB_OK);
        return 1;
    }

    return window.Run();
}
