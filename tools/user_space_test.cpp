#include "core/ControllerTask.h"
#include <QSemaphore>
#include "controllers/UserSpaceController.h"
#include "core/AppPaths.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

class UserSpaceControllerTest {
    class FakeController : public SpecialFollowController {
    public:
        QList<QPair<qint64, int>> arcs, seasons;
        QList<qint64> articles;
    private:
        void startArcFetch(qint64 mid, int page) override { arcs.append({mid, page}); }
        void startSeasonsFetch(qint64 mid, int page) override { seasons.append({mid, page}); }
        void startArticlesFetch(qint64 mid) override { articles.append(mid); }
    };
public:
    static int run() {
        int failures = 0;
        const auto check = [&](bool ok, const char *name) {
            qInfo() << (ok ? "PASS" : "FAIL") << name;
            failures += !ok;
        };
        const auto root = QJsonDocument::fromJson(R"({"data":{"card":{"mid":"3000000001","name":"Fixture","face":"http://i0.hdslb.com/avatar.jpg","sign":"hello"},"archive_count":1634}})").object();
        const UserProfile profile = UserProfileApi::parse(root);
        check(profile.mid == 3000000001LL && profile.name == "Fixture" && profile.archiveCount == 1634,
              "public profile card parses string 64-bit mid and archive count");
        check(profile.faceUrl == "https://i0.hdslb.com/avatar.jpg", "profile image normalizes to HTTPS");
        check(UserProfileApi::parse({}).mid == 0, "missing card cannot identify a different user");
        const auto largeRoot = QJsonDocument::fromJson(R"({"data":{"card":{"mid":"9007199254740993","name":"Large mid"}}})").object();
        check(UserProfileApi::parse(largeRoot).mid == 9007199254740993LL, "profile mid beyond JS Number precision remains exact");
        UserSpaceController space;
        space.selectUp(profile.mid);
        space.profile_.name = "Card fallback";
        space.profile_.faceUrl = "fallback.jpg";
        space.profileGeneration_ = 4;
        space.profileBusy_ = true;
        space.finishProfile(3, profile.mid, profile, {});
        check(space.profileName() == "Card fallback" && space.profileBusy(), "late profile generation discarded");
        space.finishProfile(4, 2, profile, {});
        check(space.profileName() == "Card fallback" && space.profileBusy(), "different profile mid discarded");
        space.finishProfile(4, profile.mid, {}, "offline failure");
        check(!space.profileBusy() && !space.profileError().isEmpty() && space.profileFace() == "fallback.jpg",
              "profile failure retains supplied identity and releases busy");
        space.finishProfile(4, profile.mid, profile, {});
        check(space.profileName() == "Fixture" && space.profileError().isEmpty(), "current profile updates metadata");
        check(!space.upsReady(), "space content selection never loads or seeds local follows");
        space.selectUp(9007199254740993LL);
        check(space.profileMid() == "9007199254740993", "profile exposes lossless QML string identity");
        space.profile_.mid = space.currentMid();
        bool accepted = QMetaObject::invokeMethod(&space, "openSpace", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("9007199254740993")), Q_ARG(QString, QStringLiteral("same")), Q_ARG(QString, QString()));
        check(accepted && space.currentMid() == 9007199254740993LL, "QML method string mid retains precision without redundant same-user fetch");
        QMetaObject::invokeMethod(&space, "openSpace", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("9223372036854775808")), Q_ARG(QString, QString()), Q_ARG(QString, QString()));
        check(space.currentMid() == 9007199254740993LL, "overflowing input mid is rejected");

        FakeController content;
        content.selectUp(1);
        content.ensureCurrentTabLoaded(0);
        content.ensureCurrentTabLoaded(1);
        content.ensureCurrentTabLoaded(2);
        content.selectUp(3000000001LL);
        content.ensureCurrentTabLoaded(0);
        content.ensureCurrentTabLoaded(1);
        content.ensureCurrentTabLoaded(2);
        content.finishArcFetch(1, 1, 0, 0, {}, {}, 3, "previous failure", false);
        content.finishSeasonsFetch(1, 1, {}, 0, {}, false);
        content.finishArticlesFetch(1, {}, {}, false);
        check(content.arcs.size() == 2 && content.arcs.last().first == 3000000001LL && content.arcBusy(),
              "new UP arc request resumes after old request fails");
        check(content.seasons.size() == 2 && content.articles.size() == 2 && content.seasonBusy() && content.articleBusy(),
              "new UP collection and article requests resume after old request completes");
        content.setArcOrder(2);
        content.setArcPartition(17);
        int staleErrors = 0;
        QObject::connect(&content, &SpecialFollowController::loadFailed, [&] { ++staleErrors; });
        content.finishArcFetch(3000000001LL, 1, 0, 0, {}, {}, 0, "obsolete failure", true);
        check(staleErrors == 0 && !content.unauthorized(), "obsolete criteria failure does not replace current page error state");
        check(content.arcItems().isEmpty() && content.arcs.size() == 3 && content.arcOrder() == 2 && content.arcTid() == 17,
              "rapid sort and partition changes discard stale result and fetch latest criteria");
        content.finishArcFetch(3000000001LL, 1, 2, 17, {QVariantMap{{"title", "latest"}}}, {}, 1, {}, false);
        check(!content.arcBusy() && content.arcItems().first().toMap().value("title") == "latest", "latest criteria publish successfully");
        content.finishSeasonsFetch(3000000001LL, 1, {}, 0, {}, false);
        content.finishArticlesFetch(3000000001LL, {}, {}, false);

        QTemporaryDir directory(QDir::currentPath() + "/user-space-test-XXXXXX");
        check(directory.isValid(), "build-local fixture directory created");
        if (!directory.isValid()) return failures;
        qputenv("BBHOUSE_DATA_DIR", directory.path().toUtf8());
        SpecialFollowStore store;
        check(store.save({{1, "existing", "", 1}}), "write isolated preexisting local follows");
        FakeController follows;
        follows.setUpFollowed(3000000001LL, "new", "", true);
        follows.setUpFollowed(2, "second", "", true);
        const auto settle = [&] {
            QElapsedTimer timer;
            timer.start();
            while ((!follows.upsReady() || follows.localFollowBusy()) && timer.elapsed() < 5000) {
                QCoreApplication::processEvents();
                QThread::msleep(1);
            }
        };
        settle();
        check(follows.containsUp(1) && follows.containsUp(3000000001LL) && follows.containsUp(2),
              "queued first-use local additions preserve unread existing file and each other");
        follows.presentedMids_.insert(1);
        follows.checkedInfo_.clear();
        follows.setUpFollowed(4, "third", "", true);
        follows.saveManage();
        follows.setUpFollowed(2, {}, {}, false);
        settle();
        check(!follows.containsUp(1) && !follows.containsUp(2) && follows.containsUp(4) && follows.containsUp(3000000001LL),
              "management and space changes serialize while preserving unpresented additions");
        const auto saved = store.load();
        check(saved.members.size() == 2 && follows.ups().size() == 2, "persisted follows match published snapshot");
        accepted = QMetaObject::invokeMethod(&follows, "setUpFollowed", Qt::DirectConnection,
            Q_ARG(QString, QStringLiteral("9007199254740993")), Q_ARG(QString, QStringLiteral("Large mid")),
            Q_ARG(QString, QString()), Q_ARG(bool, true));
        settle();
        bool contains = false;
        QMetaObject::invokeMethod(&follows, "containsUp", Qt::DirectConnection, Q_RETURN_ARG(bool, contains),
            Q_ARG(QString, QStringLiteral("9007199254740993")));
        check(accepted && contains && store.load().members.last().mid == 9007199254740993LL,
              "QML string follow methods and JSON persistence preserve greater-than-2^53 mid");
        content.releasePageCache();
        check(content.sessions_.isEmpty() && content.arcItems().isEmpty() && !content.arcBusy(),
              "release clears UP content sessions and pending gates");
        content.ensureCurrentTabLoaded(0);
        check(content.arcBusy() && content.arcs.last().second == 1,
              "returning to released UP starts its first page again");
        follows.releasePageCache();
        check(follows.containsUp(3000000001LL) && follows.upsReady(),
              "page release preserves shared local follow membership");
        space.releasePageCache();
        space.finishProfile(4, space.currentMid(), profile, {});
        check(space.profileName().isEmpty() && !space.profileBusy(),
              "space page release clears profile and rejects old profile response");
        for (qint64 mid = 100; mid < 140; ++mid) content.session(mid);
        check(content.sessions_.size() <= 12, "UP session cache is bounded");
        QSemaphore entered, proceed;
        bool deliveredAfterDestruction = false;
        auto *temporaryOwner = new QObject;
        runControllerTask(temporaryOwner, [&] {
            entered.release();
            proceed.acquire();
            return [&] { deliveredAfterDestruction = true; };
        });
        entered.acquire();
        delete temporaryOwner;
        proceed.release();
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        check(!deliveredAfterDestruction, "worker finishing after owner destruction never delivers its callback");
        qunsetenv("BBHOUSE_DATA_DIR");
        return failures;
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    return UserSpaceControllerTest::run() == 0 ? 0 : 1;
}
