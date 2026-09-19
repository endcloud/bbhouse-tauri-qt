// API 管线无头探针:最少请求冒烟(cookie 预检 → nav 信封 → 历史首页解析)。
// 用法: api-probe.exe [cookie路径]
// 退出码 0 = 全部通过。探针仅发 2 个 GET,无落库无副作用。
#include <QCoreApplication>
#include <cstdio>

#include "core/ApiErrors.h"
#include "core/BilibiliApiClient.h"
#include "core/HistoryApi.h"
#include "core/WbiSigner.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    int failures = 0;

    const QString cookiePath =
            argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString::fromStdString("bilibili.cookie.txt");

    try {
        const QString cookie = BilibiliApiClient::readCookieFromFile(cookiePath);
        std::printf("PASS cookie 预检\n");

        BilibiliApiClient *api = BilibiliApiClient::instance();

        // 1. nav + WBI 签名(经 nav 取键)
        const auto signedPair = WbiSigner::sign(*api, cookie, {{"mid", "1"}});
        if (signedPair.first.size() == 32) {
            std::printf("PASS wbi 签名 (w_rid=%s)\n", signedPair.first.toUtf8().constData());
        } else {
            failures++;
            std::printf("FAIL wbi 签名长度异常\n");
        }

        // 2. 历史首页解析
        const auto page = HistoryApi::fetchPage(*api, cookie, HistoryCursor{}, 30);
        std::printf("PASS 历史首页: %d 条, 游标 max=%lld business=%s\n",
                    page.items.size(),
                    static_cast<long long>(page.cursor.max),
                    page.cursor.business.toUtf8().constData());
        if (!page.items.isEmpty()) {
            const HistoryItem &first = page.items[0];
            const bool keyOk = first.videoKey.contains(':');
            if (keyOk) {
                std::printf("PASS 条目 video_key=%s title=%s\n",
                            first.videoKey.toUtf8().constData(),
                            first.title.toUtf8().constData());
            } else {
                failures++;
                std::printf("FAIL video_key 形态异常\n");
            }
        }
    } catch (const ApiUnauthorizedError &e) {
        failures++;
        std::printf("FAIL 登录失效: %s\n", e.what());
    } catch (const std::exception &e) {
        failures++;
        std::printf("FAIL 异常: %s\n", e.what());
    }

    std::printf(failures == 0 ? "PROBE_OK\n" : "PROBE_FAILED %d\n", failures);
    return failures == 0 ? 0 : 1;
}
