#include <QApplication>
#include <QIcon>
#include <QLibrary>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QThreadPool>
#include <QTranslator>
#include <clocale>

#include "core/HistoryServiceEntry.h"
#include "core/HistoryServiceWorker.h"
#include "core/DeploymentSmoke.h"
#include "controllers/HistoryServiceController.h"
#include "controllers/AppController.h"
#include "controllers/LoginController.h"
#include "controllers/BangumiController.h"
#include "controllers/DynamicsController.h"
#include "controllers/HistoryController.h"
#include "controllers/OnlineHistoryController.h"
#include "controllers/SpecialFollowController.h"
#include "controllers/UserSpaceController.h"
#include "controllers/WatchlaterController.h"
#include "controllers/LiveController.h"
#include "controllers/PopularController.h"
#include "player/LivePlayerController.h"
#include "preferences/AppPreferences.h"
#include "player/MpvLib.h"
#include "player/PlayerController.h"
#include "downloads/DownloadController.h"

int main(int argc, char *argv[]) {
    // Isolated deployment verification must run before preferences, services or
    // normal controllers can inspect the current user's data.
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--deployment-smoke-test")
            return runDeploymentSmokeTest(argc, argv);
    QCoreApplication::setOrganizationName("shizi");
    QCoreApplication::setApplicationName("bbhouse-qt");
    // SCM worker branches before GUI, mpv, QML, preferences or user-path probing.
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--history-service-worker")
            return runHistoryServiceWorker(argc, argv);
    // Scheduled work never initializes GUI, mpv, QML, or unrelated controllers.
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--history-sync-once")
            return runHistoryServiceEntry(argc, argv);
#ifdef Q_OS_WIN
    // 跟随系统明暗(标题栏/主题),FluentUI 要求 Basic 样式
    qputenv("QT_QPA_PLATFORM", "windows:darkmode=2");
#endif
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

#ifdef Q_OS_MACOS
    // macOS 默认仅提供 OpenGL 2.1 兼容上下文,mpv render API 需要 3.2 Core;
    // 须在首个 GL 上下文创建前设置(Windows 维持既有默认,不动)
    QSurfaceFormat glFormat;
    glFormat.setVersion(3, 2);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(glFormat);
#endif

    QApplication::setOrganizationName("shizi");
    QApplication::setApplicationName("bbhouse-qt");
    QApplication::setApplicationVersion(QStringLiteral(BBHOUSE_APP_VERSION));
    QApplication app(argc, argv);
    // Keep applicationName stable for existing data paths; brand uses the display name.
    QApplication::setApplicationDisplayName(QStringLiteral("BBHouse"));
#ifdef Q_OS_MACOS
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/bbhouse-icon-1024-mac.png")));
#else
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/bbhouse-icon-1024.png")));
#endif
#ifdef Q_OS_LINUX
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.endcloud.bbhouse-qt"));
#endif
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    // libmpv 客户端 API 强制要求 C locale(QApplication 会按系统区域设置
    // LC_NUMERIC,mpv 在非 C locale 下数字解析未定义,可致参数/命令失效)
    std::setlocale(LC_NUMERIC, "C");

    // i18n: zh-CN 为中性源语言,en-US 走翻译;跟随系统时按系统语言链匹配
    auto *preferences = AppPreferences::instance();
    QTranslator translator;
    const QString lang = preferences->language();
    bool useEnglish = (lang == "en_US");
    if (lang == "system") {
        useEnglish = !QLocale::system().name().startsWith("zh");
    }
    if (useEnglish) {
        if (!translator.load(":/i18n/bbhouse_en_US.qm")) {
            qWarning() << "load bbhouse_en_US.qm failed";
        }
        app.installTranslator(&translator);
    }

    // mpv 内核探测:exe 同级优先,失败不阻塞启动(播放窗口再行降级)
    MpvLib::instance()->probe();

    QQmlApplicationEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());
    engine.rootContext()->setContextProperty("AppPreferences", preferences);
    engine.rootContext()->setContextProperty("MpvLib", MpvLib::instance());

    // Controller 桥接(命名与 WinUI 页面对应,见 doc/qt-migration-notes.md)
    auto *appController = new AppController(&app);
    auto *loginController = new LoginController(&app);
    auto *historyController = new HistoryController(&app);
    auto *historyServiceController = new HistoryServiceController(&app);
    // 动态流 / 在线历史 / 稍后再看:云端只读页桥接(动态 offset 续载池 / 游标续载池 / 单次全量池)
    auto *dynamicsController = new DynamicsController(&app);
    auto *onlineHistoryController = new OnlineHistoryController(&app);
    auto *watchlaterController = new WatchlaterController(&app);
    // 追番追剧:两桶标准分页 + 剧集详情会话缓存(bangumi-ui)
    auto *bangumiController = new BangumiController(&app);
    // 特别关注:本地 UP 快照 + 单 UP 三档会话缓存 + 管理弹层(special-follow-ui)
    auto *specialFollowController = new SpecialFollowController(&app);
    auto *userSpaceController = new UserSpaceController(&app);
    // 播放窗口管线:videoItem/danmakuItem 由其在 C++ 创建,PlayerWindow.qml 仅挂载
    auto *playerController = new PlayerController(&app);
    auto *downloadController = new DownloadController(&app);
    auto *liveController = new LiveController(&app);
    auto *popularController = new PopularController(&app);
    // 流行榜单的整季详情独立于追番页，避免快速跨页起播互相取消。
    auto *popularSeasonController = new BangumiController(&app);
    auto *livePlayerController = new LivePlayerController(&app);
    engine.rootContext()->setContextProperty("AppController", appController);
    engine.rootContext()->setContextProperty("LoginController", loginController);
    engine.rootContext()->setContextProperty("HistoryController", historyController);
    engine.rootContext()->setContextProperty("HistoryServiceController", historyServiceController);
    engine.rootContext()->setContextProperty("DynamicsController", dynamicsController);
    engine.rootContext()->setContextProperty("OnlineHistoryController", onlineHistoryController);
    engine.rootContext()->setContextProperty("WatchlaterController", watchlaterController);
    engine.rootContext()->setContextProperty("BangumiController", bangumiController);
    engine.rootContext()->setContextProperty("SpecialFollowController", specialFollowController);
    engine.rootContext()->setContextProperty("UserSpaceController", userSpaceController);
    engine.rootContext()->setContextProperty("PlayerController", playerController);
    engine.rootContext()->setContextProperty("DownloadController", downloadController);
    engine.rootContext()->setContextProperty("LiveController", liveController);
    engine.rootContext()->setContextProperty("PopularController", popularController);
    engine.rootContext()->setContextProperty("PopularSeasonController", popularSeasonController);
    engine.rootContext()->setContextProperty("LivePlayerController", livePlayerController);

    // 冒烟导航:BBHOUSE_SMOKE_NAV=qml 文件名列表(逗号分隔),由 MainWindow 逐页导航
    engine.rootContext()->setContextProperty("bbhouseSmokeNav",
                                             qEnvironmentVariable("BBHOUSE_SMOKE_NAV"));
    // 冒烟起播:BBHOUSE_SMOKE_PLAY_AID=<aid> 时构造 archive 条目直接进播放窗口
    engine.rootContext()->setContextProperty("bbhouseSmokePlayAid",
                                             qEnvironmentVariable("BBHOUSE_SMOKE_PLAY_AID"));

    const QUrl url(QStringLiteral("qrc:/qt/qml/bbhouse/qml/Main.qml"));
    QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreated, &app,
            [url](QObject *obj, const QUrl &objUrl) {
                if (!obj && url == objUrl) QCoreApplication::exit(-1);
            },
            Qt::QueuedConnection);

    engine.load(url);

    // 冒烟模式:BBHOUSE_SMOKE=1 时自动退出,供无头环境验证 QML 装载
    if (qEnvironmentVariable("BBHOUSE_SMOKE") == "1") {
        QTimer::singleShot(3000, &app, &QCoreApplication::quit);
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, historyController, &HistoryController::cancelSync);
    const int exitCode = app.exec();
    // 线程池任务持有 Controller，必须在 QApplication 销毁其 children 前收尾。
    QThreadPool::globalInstance()->waitForDone();
    return exitCode;
}
