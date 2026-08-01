#pragma once

#include <QString>

#include <memory>

namespace wimi::client
{

class IPlatformServices
{
  public:
    virtual ~IPlatformServices() = default;

    virtual QString PlatformName() const = 0;
    virtual bool DesktopNotificationsAvailable() const = 0;
    virtual bool ShowDesktopNotification(const QString& title, const QString& body) = 0;
};

std::unique_ptr<IPlatformServices> CreatePlatformServices();

} // namespace wimi::client
