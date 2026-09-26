#pragma once

#include <QCoreApplication>
#include <QPointer>
#include <QQmlEngine>

/**
 * @brief The Singleton class
 */
template <typename T>
class Singleton {
public:
    static T *getInstance();
};

template <typename T>
T *Singleton<T>::getInstance() {
    // Several QML engines may use the same singleton in one process. An
    // engine must not delete the shared instance and leave this cache dangling.
    static QPointer<T> instance;
    if (!instance) {
        instance = new T();
        instance->setParent(QCoreApplication::instance());
        QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);
    }
    return instance.data();
}

#define SINGLETON(Class)                                                                           \
private:                                                                                           \
    friend class Singleton<Class>;                                                                 \
                                                                                                   \
public:                                                                                            \
    static Class *getInstance() {                                                                  \
        return Singleton<Class>::getInstance();                                                    \
    }
