#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

#include <cstdint>

namespace wimi::client
{

struct GateSession {
    std::int64_t uid{};
    QString gatewayHost;
    quint16 gatewayPort{};
    QString gatewayId;
    QString token;
    qint64 tokenExpiresInSeconds{};
    QString profileName;
    bool profileInitializationRequired{};
};

class GateHttpClient final : public QObject
{
    Q_OBJECT

  public:
    explicit GateHttpClient(QObject* parent = nullptr);

    void SetBaseUrl(QUrl baseUrl);
    QUrl BaseUrl() const;

    void RequestVerificationCode(const QString& email);
    void SignUp(const QString& username, const QString& password, const QString& email, const QString& verifyCode);
    void SignIn(const QString& username, const QString& password);
    void ForgetPassword(const QString& username, const QString& email, const QString& verifyCode,
                        const QString& newPassword);

  signals:
    void SignInSucceeded(const wimi::client::GateSession& session);
    void OperationSucceeded(const QString& operation, const QJsonObject& response);
    void OperationFailed(const QString& operation, int errorCode, const QString& message);

  private:
    void Post(const QString& operation, const QString& path, const QJsonObject& body);
    QUrl Endpoint(const QString& path) const;

    QNetworkAccessManager network_;
    QUrl base_url_;
};

} // namespace wimi::client

Q_DECLARE_METATYPE(wimi::client::GateSession)
