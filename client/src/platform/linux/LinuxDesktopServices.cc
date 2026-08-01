#include "platform/linux/LinuxDesktopServices.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QVariantMap>

namespace wimi::client
{
namespace
{

constexpr auto kNotificationService = "org.freedesktop.Notifications";
constexpr auto kNotificationPath = "/org/freedesktop/Notifications";
constexpr auto kNotificationInterface = "org.freedesktop.Notifications";

QDBusInterface NotificationInterface()
{
    return QDBusInterface(QString::fromLatin1(kNotificationService), QString::fromLatin1(kNotificationPath),
                          QString::fromLatin1(kNotificationInterface), QDBusConnection::sessionBus());
}

} // namespace

QString LinuxDesktopServices::PlatformName() const { return QStringLiteral("linux"); }

bool LinuxDesktopServices::DesktopNotificationsAvailable() const
{
    if (!QDBusConnection::sessionBus().isConnected()) {
        return false;
    }
    return NotificationInterface().isValid();
}

bool LinuxDesktopServices::ShowDesktopNotification(const QString& title, const QString& body)
{
    QDBusInterface notifications = NotificationInterface();
    if (!notifications.isValid()) {
        return false;
    }

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("wimi-client"));
    const QList<QVariant> arguments = {
        QStringLiteral("WIMI"), 0U, QString{}, title, body, QStringList{}, hints, 5000,
    };
    const QDBusPendingCall call = notifications.asyncCallWithArgumentList(QStringLiteral("Notify"), arguments);
    return !call.isError();
}

} // namespace wimi::client
