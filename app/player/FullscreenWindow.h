#pragma once

#include <QWindow>

namespace PlayerFullscreen {

// Keep the custom FluentUI frame out of the native window while entering
// display fullscreen. The original style is restored when leaving fullscreen.
void setBorderless(QWindow *window, bool borderless);

}  // namespace PlayerFullscreen
