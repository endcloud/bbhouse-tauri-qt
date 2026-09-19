#include <QApplication>
#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <memory>

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    QApplication app(argc, argv);
    QQmlEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    QQmlComponent pageComponent(&engine);
    pageComponent.setData("import QtQml; QtObject { property string searchQuery: \"\" }", QUrl());
    std::unique_ptr<QObject> first(pageComponent.create()), second(pageComponent.create());
    QQmlComponent component(&engine, QUrl::fromLocalFile(SEARCHBOX_QML_PATH));
    std::unique_ptr<QObject> editor(component.create());
    auto *item = qobject_cast<QQuickItem *>(editor.get());
    if (!first || !second || !item) {
        qCritical() << pageComponent.errors() << component.errors();
        return 1;
    }
    QQuickWindow window;
    item->setParentItem(window.contentItem());
    window.show();
    app.processEvents();
    int failures = 0;
    auto check = [&](bool ok, const char *label) {
        qInfo() << (ok ? "PASS" : "FAIL") << label;
        if (!ok) ++failures;
    };
    auto bindPage = [&](QObject *page) {
        editor->setProperty("searchPage", QVariant::fromValue(page));
    };

    bindPage(first.get());
    item->forceActiveFocus();
    editor->setProperty("text", "Alpha");
    check(first->property("searchQuery").toString().isEmpty(), "typing does not submit");
    // FluTextBox consumes Enter/Return and emits commit instead of editingFinished.
    QMetaObject::invokeMethod(editor.get(), "commit", Q_ARG(QString, QStringLiteral("Alpha")));
    check(first->property("searchQuery") == "Alpha", "FluentUI commit submits to current page");

    editor->setProperty("text", "Beta");
    check(item->hasActiveFocus(), "editor has active focus before blur");
    item->setFocus(false);
    check(first->property("searchQuery") == "Beta", "focus loss submits without Enter");
    bindPage(second.get());
    check(editor->property("text").toString().isEmpty(), "new page starts with its own empty query");
    editor->setProperty("text", "Gamma");
    QMetaObject::invokeMethod(editor.get(), "submit");
    check(first->property("searchQuery") == "Beta" && second->property("searchQuery") == "Gamma",
          "submitting another page leaves previous query untouched");
    bindPage(nullptr);
    check(editor->property("text").toString().isEmpty(), "non-search page clears displayed text");
    bindPage(first.get());
    check(editor->property("text") == "Beta", "returning restores original page query");
    item->setFocus(false);
    editor->setProperty("text", "");
    check(first->property("searchQuery") == "Beta" && item->hasActiveFocus(),
          "clear from an unfocused editor restores focus and remains a draft");
    item->setFocus(false);
    check(first->property("searchQuery").toString().isEmpty() && second->property("searchQuery") == "Gamma",
          "empty submission clears only the current page filter");
    item->setParentItem(nullptr);
    return failures ? 1 : 0;
}
