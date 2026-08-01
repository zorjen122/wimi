#include "adapters/gate/GateHttpClient.h"

#include "adapters/connection_gateway/ClientProtocol.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <utility>

namespace wimi::client
{

GateHttpClient::GateHttpClient(QObject* parent) : QObject(parent) { qRegisterMetaType<GateSession>(); }

void GateHttpClient::SetBaseUrl(QUrl baseUrl) { base_url_ = std::move(baseUrl); }

QUrl GateHttpClient::BaseUrl() const { return base_url_; }

void GateHttpClient::RequestVerificationCode(const QString& email)
{
    Post(QStringLiteral("verify-code"), QStringLiteral("/post-verifycode"), {{QStringLiteral("email"), email}});
}

void GateHttpClient::SignUp(const QString& username, const QString& password, const QString& email,
                            const QString& verifyCode)
{
    Post(QStringLiteral("sign-up"), QStringLiteral("/post-signUp"),
         {{QStringLiteral("username"), username},
          {QStringLiteral("password"), password},
          {QStringLiteral("email"), email},
          {QStringLiteral("verifycode"), verifyCode}});
}

void GateHttpClient::SignIn(const QString& username, const QString& password)
{
    Post(QStringLiteral("sign-in"), QStringLiteral("/post-signIn"),
         {{QStringLiteral("username"), username}, {QStringLiteral("password"), password}});
}

void GateHttpClient::ForgetPassword(const QString& username, const QString& email, const QString& verifyCode,
                                    const QString& newPassword)
{
    Post(QStringLiteral("forget-password"), QStringLiteral("/post-forget-password"),
         {{QStringLiteral("username"), username},
          {QStringLiteral("email"), email},
          {QStringLiteral("verifycode"), verifyCode},
          {QStringLiteral("password"), newPassword}});
}

void GateHttpClient::Post(const QString& operation, const QString& path, const QJsonObject& body)
{
    if (!base_url_.isValid() || base_url_.scheme().isEmpty() || base_url_.host().isEmpty()) {
        emit OperationFailed(operation, protocol::JsonParser, tr("Auth Gate 地址无效"));
        return;
    }

    QNetworkRequest request(Endpoint(path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto* reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, operation, body] {
        const QByteArray payload = reply->readAll();
        const auto networkError = reply->error();
        const QString networkMessage = reply->errorString();
        reply->deleteLater();

        if (networkError != QNetworkReply::NoError) {
            emit OperationFailed(operation, protocol::DependencyUnavailable, networkMessage);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit OperationFailed(operation, protocol::JsonParser, tr("Auth Gate 返回了无效 JSON"));
            return;
        }

        const QJsonObject response = document.object();
        const int errorCode = response.value(QStringLiteral("error")).toVariant().toInt();
        if (errorCode != protocol::Success) {
            emit OperationFailed(operation, errorCode,
                                 response.value(QStringLiteral("message")).toString().isEmpty()
                                     ? tr("Auth Gate 请求失败（%1）").arg(errorCode)
                                     : response.value(QStringLiteral("message")).toString());
            return;
        }

        if (operation == QStringLiteral("sign-in")) {
            GateSession session{
                .uid = response.value(QStringLiteral("uid")).toVariant().toLongLong(),
                .gatewayHost = response.value(QStringLiteral("ip")).toString(),
                .gatewayPort = static_cast<quint16>(response.value(QStringLiteral("port")).toInt()),
                .gatewayId = response.value(QStringLiteral("gatewayId")).toString(),
                .token = response.value(QStringLiteral("chatToken")).toString(),
                .tokenExpiresInSeconds = response.value(QStringLiteral("chatTokenExpiresIn")).toVariant().toLongLong(),
                .profileName = body.value(QStringLiteral("username")).toString(),
                .profileInitializationRequired = response.value(QStringLiteral("init")).toInt() != 0,
            };
            if (session.uid <= 0 || session.gatewayHost.isEmpty() || session.gatewayPort == 0 ||
                session.token.isEmpty()) {
                emit OperationFailed(operation, protocol::JsonParser, tr("登录响应缺少 Gateway 会话字段"));
                return;
            }
            emit SignInSucceeded(session);
        }
        emit OperationSucceeded(operation, response);
    });
}

QUrl GateHttpClient::Endpoint(const QString& path) const
{
    QUrl endpoint = base_url_;
    endpoint.setPath(path);
    endpoint.setQuery(QString{});
    endpoint.setFragment(QString{});
    return endpoint;
}

} // namespace wimi::client
