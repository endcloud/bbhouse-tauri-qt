#include "FluTreeModel.h"
#include "singleton.h"

#include <QCoreApplication>
#include <QDebug>
#include <QPointer>

class OwnedSingleton : public QObject {
    SINGLETON(OwnedSingleton)
private:
    OwnedSingleton() = default;
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    FluTreeModel model;
    const QList<QVariantMap> source{
        {{"title", "root"}, {"children", QVariantList{
            QVariantMap{{"title", "child"}, {"children", QVariantList{
                QVariantMap{{"title", "grandchild"}}}}}}}}};
    int failures = 0;
    auto check = [&](bool ok, const char *name) {
        qInfo() << (ok ? "PASS" : "FAIL") << name;
        failures += !ok;
    };
    const auto singleton = OwnedSingleton::getInstance();
    check(singleton == OwnedSingleton::getInstance() && singleton->parent() == &app
          && QQmlEngine::objectOwnership(singleton) == QQmlEngine::CppOwnership,
          "shared singletons have an application owner and cannot be deleted by a QML engine");
    model.setDataSource(source);
    QPointer<FluTreeNode> oldRoot = model.getNode(0);
    QPointer<FluTreeNode> oldChild = model.getNode(1);
    QPointer<FluTreeNode> oldGrandchild = model.getNode(2);
    model.collapse(0);
    check(model.rowCount() == 1 && !oldChild.isNull(), "collapse retains hidden nodes for re-expansion");
    model.expand(0);
    check(model.rowCount() == 3 && model.getNode(1) == oldChild,
          "expand reuses the same owned subtree");
    bool resetSawOldRows = false;
    QObject::connect(&model, &QAbstractItemModel::modelAboutToBeReset, &model, [&] {
        resetSawOldRows = model.rowCount() == 3 && !oldRoot.isNull();
    });
    model.setDataSource(source);
    check(resetSawOldRows && oldRoot.isNull() && oldChild.isNull() && oldGrandchild.isNull(),
          "reset announces old rows before releasing all old nodes");
    for (int i = 0; i < 128; ++i) model.setDataSource(source);
    check(model.findChildren<FluTreeNode *>().size() == 4,
          "repeated source replacement retains only one root and the live subtree");
    model.setDataSource({});
    check(model.rowCount() == 0 && model.findChildren<FluTreeNode *>().size() == 1,
          "empty source releases collapsed and visible nodes");
    return failures ? 1 : 0;
}
