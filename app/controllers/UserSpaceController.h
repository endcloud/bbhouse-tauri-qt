#ifndef USER_SPACE_CONTROLLER_H
#define USER_SPACE_CONTROLLER_H
#include "controllers/SpecialFollowController.h"
#include "core/UserProfileApi.h"

// 独立于特别关注页的空间会话；不装载关注名单，避免名单回落覆盖路由 mid。
class UserSpaceController : public SpecialFollowController {
    Q_OBJECT
    Q_PROPERTY(QString profileMid READ profileMid NOTIFY currentMidChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY profileChanged)
    Q_PROPERTY(QString profileFace READ profileFace NOTIFY profileChanged)
    Q_PROPERTY(QString profileSign READ profileSign NOTIFY profileChanged)
    Q_PROPERTY(qint64 profileArchiveCount READ profileArchiveCount NOTIFY profileChanged)
    Q_PROPERTY(bool profileBusy READ profileBusy NOTIFY profileBusyChanged)
    Q_PROPERTY(QString profileError READ profileError NOTIFY profileChanged)
public:
    explicit UserSpaceController(QObject *parent = nullptr);
    Q_INVOKABLE void releasePageCache() override;
    QString profileMid() const { return QString::number(currentMid()); }
    QString profileName() const { return profile_.name; }
    QString profileFace() const { return profile_.faceUrl; }
    QString profileSign() const { return profile_.sign; }
    qint64 profileArchiveCount() const { return profile_.archiveCount; }
    bool profileBusy() const { return profileBusy_; }
    QString profileError() const { return profileError_; }
    Q_INVOKABLE void openSpace(const QString &midText, const QString &name = {}, const QString &face = {});
    Q_INVOKABLE void refreshProfile();
signals:
    void profileChanged();
    void profileBusyChanged();
private:
    friend class UserSpaceControllerTest;
    void finishProfile(quint64 generation, qint64 mid, const UserProfile &profile, const QString &error);
    UserProfile profile_;
    QString profileError_;
    bool profileBusy_ = false;
    quint64 profileGeneration_ = 0;
};
#endif
