#ifndef LOGIN_CONTROLLER_H
#define LOGIN_CONTROLLER_H
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <functional>

class QNetworkReply;
class LoginController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool needsLogin READ needsLogin NOTIFY needsLoginChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString qrUrl READ qrUrl NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString cookiePath READ cookiePath CONSTANT)
public:
    explicit LoginController(QObject *parent = nullptr);
    bool needsLogin() const { return needsLogin_; }
    bool busy() const { return busy_; }
    QString qrUrl() const { return qrUrl_; }
    QString status() const { return status_; }
    QString error() const { return error_; }
    QString cookiePath() const { return cookiePath_; }
    Q_INVOKABLE void open();
    Q_INVOKABLE void startQr();
    Q_INVOKABLE void importText(const QString &text);
    Q_INVOKABLE void importFile(const QUrl &file);
    Q_INVOKABLE void cancel();
signals:
    void stateChanged();
    void needsLoginChanged();
    void loginRequested();
    void authenticated();
protected:
    // Async transport seam for isolated offline regression; production always uses direct HTTPS.
    virtual void get(const QUrl &url, const QString &cookie, std::function<void(QNetworkReply *)> done);
private:
    void poll();
    void validateAndSave(const QString &cookie);
    void fail(const QString &message);
    void finish(const QString &cookie);
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer pollTimer_;
    QString cookiePath_, qrUrl_, qrKey_, status_, error_;
    bool needsLogin_ = true;
    bool busy_ = false;
    int generation_ = 0;
    qint64 qrDeadline_ = 0;
};
#endif
