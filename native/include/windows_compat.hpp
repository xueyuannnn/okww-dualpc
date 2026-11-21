#pragma once
#include <windows.h>

namespace okww::wincompat {
namespace detail {
using DpiQuery = UINT(WINAPI *)(HWND);

inline UINT windowDpi(HWND window, DpiQuery query) noexcept {
    if (query) return query(window);
    // Win7 has system-wide DPI. Keep capture and input in the same coordinate
    // space without requiring a per-monitor DPI entry point at load time.
    const auto dc = GetDC(window);
    if (!dc) return 96;
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(window, dc);
    return dpi > 0 ? UINT(dpi) : 96;
}
} // namespace detail

inline UINT dpiForWindow(HWND window) noexcept {
    static const auto query = [] {
        const auto user = GetModuleHandleW(L"user32.dll");
        return user ? reinterpret_cast<detail::DpiQuery>(GetProcAddress(user, "GetDpiForWindow")) : nullptr;
    }();
    return detail::windowDpi(window, query);
}

inline DPI_AWARENESS_CONTEXT setThreadDpiContext(DPI_AWARENESS_CONTEXT context) noexcept {
    using SetContext = DPI_AWARENESS_CONTEXT(WINAPI *)(DPI_AWARENESS_CONTEXT);
    static const auto setContext = [] {
        const auto user = GetModuleHandleW(L"user32.dll");
        return user ? reinterpret_cast<SetContext>(GetProcAddress(user, "SetThreadDpiAwarenessContext")) : nullptr;
    }();
    return setContext ? setContext(context) : nullptr;
}

inline void describeCurrentThread(PCWSTR name) noexcept {
    using DescribeThread = HRESULT(WINAPI *)(HANDLE, PCWSTR);
    static const auto describe = [] {
        const auto kernel = GetModuleHandleW(L"kernel32.dll");
        return kernel ? reinterpret_cast<DescribeThread>(GetProcAddress(kernel, "SetThreadDescription")) : nullptr;
    }();
    if (describe) describe(GetCurrentThread(), name);
}
} // namespace okww::wincompat
