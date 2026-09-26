#pragma once
#include <QSemaphore>
#include <atomic>
#include <stdexcept>
#include "player/OnlineDanmakuLoader.h"

// Uses a real worker/queued UI delivery and the production XML parser, offline.
// The caller supplies its normal event-loop wait/assert helpers.
template<class Check, class Until>
void checkOnlineDanmakuReuse(Check check, Until until) {
    const auto fixture = [](qint64 cid) {
        return QStringLiteral("<i><d p=\"1,1,25,16777215,0,0,fixture,1\">%1</d></i>").arg(cid);
    };
    {
        std::atomic_int requests{0};
        QSemaphore gate;
        int delivered = 0;
        OnlineDanmakuLoader loader(nullptr, [&](qint64 cid, const QString &) {
            ++requests;
            gate.acquire();
            return fixture(cid);
        });
        loader.loaded = [&](QVariantList entries) {
            ++delivered;
            check(entries.size() == 1 && entries.first().toMap().value("message") == "101",
                  "pending same-content result survives quality replacement");
        };
        loader.request(101, "fixture-account");
        check(until([&] { return requests == 1; }), "online danmaku starts off-thread");
        for (int i = 0; i < 5; ++i) loader.request(101, "fixture-account");
        gate.release(8); // Never leave a worker blocked when a check fails.
        check(until([&] { return delivered == 1; }), "one parsed result delivered for repeated pending request");
        loader.request(101, "fixture-account");
        QCoreApplication::processEvents();
        check(requests == 1 && delivered == 1, "quality changes reuse loaded data without XML fetch/parse/reupload");
    }
    {
        std::atomic_int requests{0};
        QSemaphore gate;
        QVariantList delivered;
        OnlineDanmakuLoader loader(nullptr, [&](qint64 cid, const QString &) {
            ++requests;
            gate.acquire();
            return fixture(cid);
        });
        loader.loaded = [&](QVariantList entries) { delivered.append(entries); };
        loader.request(101, "fixture-account");
        check(until([&] { return requests == 1; }), "old-content fixture is pending");
        loader.reset();
        loader.request(202, "fixture-account");
        gate.release(4);
        check(until([&] { return !delivered.isEmpty(); }) && delivered.size() == 1
                  && delivered.first().toMap().value("message") == "202",
              "new content discards old queued result and keeps new content");
        loader.request(202, "other-fixture-account");
        check(until([&] { return requests == 3 && delivered.size() == 2; }),
              "same cid under changed account context reloads");
        loader.reset();
        loader.request(303, "fixture-account");
        loader.reset();
        gate.release(4);
        QCoreApplication::processEvents();
        check(delivered.size() == 2, "close/reset does not publish pending content");
    }
    {
        std::atomic_int requests{0};
        int errors = 0, delivered = 0;
        OnlineDanmakuLoader loader(nullptr, [&](qint64, const QString &) {
            if (++requests == 1) throw std::runtime_error("offline fixture failure");
            return QStringLiteral("<i/>");
        });
        loader.loaded = [&](QVariantList entries) { ++delivered; check(entries.isEmpty(), "empty successful archive is accepted"); };
        loader.failed = [&](QString) { ++errors; };
        loader.request(404, "fixture-account");
        check(until([&] { return errors == 1; }), "optional fetch failure is delivered once");
        loader.request(404, "fixture-account");
        check(until([&] { return delivered == 1; }) && requests == 2, "later quality switch retries failed archive");
        loader.request(404, "fixture-account");
        check(requests == 2, "successful empty archive is not downloaded repeatedly");
    }
}
