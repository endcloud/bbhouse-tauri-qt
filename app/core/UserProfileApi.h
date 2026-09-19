#ifndef USER_PROFILE_API_H
#define USER_PROFILE_API_H

#include <QJsonObject>
#include <QString>

class BilibiliApiClient;
struct UserProfile {
    qint64 mid = 0;
    QString name;
    QString faceUrl;
    QString sign;
    qint64 archiveCount = 0;
};

// 用户名片 GET /x/web-interface/card；user/info.md 的公开资料接口。
// 只读资料，不读取或修改远端关注关系。
class UserProfileApi {
public:
    static UserProfile fetch(BilibiliApiClient &api, qint64 mid);
    static UserProfile parse(const QJsonObject &root);
};
#endif
