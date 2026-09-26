#include "player/SystemMediaWindowsSmoke.h"

#include <QCoreApplication>
#include <QDebug>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString error = verifyWindowsSystemMediaBackend();
    if (!error.isEmpty()) {
        qCritical().noquote() << error;
        return 1;
    }
    qInfo() << "SYSTEM_MEDIA_WINDOWS_OK: native attach, metadata/timeline readback, reset and reattach";
    return 0;
}
