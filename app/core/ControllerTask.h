#pragma once

#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>
#include <functional>
#include <memory>

// Workers return a GUI-thread completion instead of posting to a raw QObject.
// The parent-owned watcher disconnects delivery if the controller is destroyed.
// Work must capture independent data; a raw controller may only be captured by
// the returned completion, never dereferenced on the worker thread.
template<class Work>
void runControllerTask(QObject *owner, Work work) {
    using Completion = std::function<void()>;
    auto *watcher = new QFutureWatcher<Completion>(owner);
    QObject::connect(watcher, &QFutureWatcher<Completion>::finished, owner, [watcher] {
        const auto completion = watcher->result();
        watcher->deleteLater();
        if (completion) completion();
    });
    auto promise = std::make_shared<QPromise<Completion>>();
    promise->start();
    watcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, work = std::move(work)]() mutable {
        promise->addResult(work());
        promise->finish();
    });
}
