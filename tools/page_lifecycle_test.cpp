#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QPointer>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <memory>

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    QQmlComponent page(&engine);
    page.setData("import QtQuick; Item { property string searchQuery: \"draft\" }", QUrl());
    QQmlComponent component(&engine, QUrl::fromLocalFile(PAGE_LOADER_QML_PATH));
    std::unique_ptr<QObject> loader(component.create());
    if (!loader || page.isError()) { qCritical() << component.errors() << page.errors(); return 1; }
    loader->setProperty("sourceComponent", QVariant::fromValue(&page));
    loader->setProperty("retentionMs", 50);
    auto wait = [](int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); };
    auto item = [&] { return qvariant_cast<QObject *>(loader->property("item")); };
    int failures = 0;
    auto check = [&](bool value, const char *label) { qInfo() << (value ? "PASS" : "FAIL") << label; failures += !value; };
    check(!item(), "unvisited page is not instantiated");
    loader->setProperty("pageActive", true);
    QPointer<QObject> first(item());
    check(first, "first visit creates page");
    if (!first) return 1;
    first->setProperty("searchQuery", "kept");
    wait(100);
    check(item() == first && first, "active page never expires");
    loader->setProperty("pageActive", false);
    // Synchronous navigation back must cancel the pending background expiry.
    loader->setProperty("pageActive", true);
    wait(100);
    check(item() == first && first->property("searchQuery") == "kept", "short return preserves instance and state");
    loader->setProperty("pageActive", false);
    wait(120);
    check(!item() && !first, "timeout destroys the actual QML object");
    loader->setProperty("pageActive", true);
    check(item() && item()->property("searchQuery") == "draft", "return after expiry reconstructs view");
    loader->setProperty("retentionMs", 500);
    loader->setProperty("pageActive", false);
    loader->setProperty("retentionMs", 40);
    wait(110);
    check(!item(), "changed threshold restarts background expiry");
    for (int n = 0; n < 5; ++n) {
        loader->setProperty("pageActive", true);
        QPointer<QObject> previous(item());
        loader->setProperty("pageActive", false);
        wait(100);
        check(!previous && !item(), "repeated expiration leaves no retained page");
    }
    return failures ? 1 : 0;
}
