#ifndef API_ERRORS_H
#define API_ERRORS_H

#include <QCoreApplication>
#include <QString>
#include <stdexcept>
#include "core/DiagnosticPrivacy.h"

// 核心层 zh 中性文案 + en 翻译(经 .ts context "core")。占位符由调用方 .arg 填充。
class Loc {
   public:
    static QString get(const char *source) {
        return QCoreApplication::translate("core", source);
    }
};

// API 层错误:信封非零 code 或 HTTP 非 2xx。
class ApiError : public std::runtime_error {
   public:
    ApiError(int code, const QString &message, int httpStatus = 0)
        : std::runtime_error(privateDiagnostic(message).toStdString()),
          code_(code),
          httpStatus_(httpStatus),
          message_(privateDiagnostic(message)) {}

    int code() const { return code_; }
    int httpStatus() const { return httpStatus_; }
    const QString &message() const { return message_; }

   private:
    int code_;
    int httpStatus_;
    QString message_;
};

// 登录失效(code=-101)语义,调用方按类型统一处理。
class ApiUnauthorizedError : public ApiError {
   public:
    explicit ApiUnauthorizedError(const QString &message)
        : ApiError(-101, message) {}
};

#endif  // API_ERRORS_H
