#pragma once

#include <QString>

#ifdef Q_OS_WIN
// Empty only after native hidden-window attach, metadata/timeline readback,
// reset, and reattach succeed. Shared by CTest and the deployed executable.
QString verifyWindowsSystemMediaBackend();
#endif
