#include "player/FullscreenWindow.h"

#include <QHash>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace PlayerFullscreen {

#ifdef Q_OS_WIN
namespace {
QHash<quintptr, LONG_PTR> savedStyles;
}
#endif

void setBorderless(QWindow *window, bool borderless) {
    if (!window) return;

#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return;
    const quintptr key = reinterpret_cast<quintptr>(hwnd);

    if (borderless) {
        if (savedStyles.contains(key)) return;
        const LONG_PTR style = ::GetWindowLongPtr(hwnd, GWL_STYLE);
        savedStyles.insert(key, style);
        const LONG_PTR clientOnlyStyle = style & ~(WS_CAPTION | WS_THICKFRAME |
                                                    WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
                                                    WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
        ::SetWindowLongPtr(hwnd, GWL_STYLE, clientOnlyStyle);
        ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                           SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW |
                                                   RDW_FRAME);
        return;
    }

    const auto it = savedStyles.constFind(key);
    if (it == savedStyles.constEnd()) return;
    ::SetWindowLongPtr(hwnd, GWL_STYLE, it.value());
    savedStyles.erase(it);
    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    ::RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW |
                                               RDW_FRAME);
#else
    Q_UNUSED(borderless)
#endif
}

}  // namespace PlayerFullscreen
