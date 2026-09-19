#pragma once

#include <QVariantList>

// Stateless load-time preprocessing, safe to run in a worker thread. Always
// pass raw entries; rendered/count-suffixed output must not become the source.
class DanmakuFilter {
public:
    static QVariantList mergeSimilar(const QVariantList &entries);
};
