#pragma once

#include "ports/IPlatformServices.h"

namespace wimi::client
{

class LinuxDesktopServices final : public IPlatformServices
{
  public:
    QString PlatformName() const override;
    bool DesktopNotificationsAvailable() const override;
    bool ShowDesktopNotification(const QString& title, const QString& body) override;
};

} // namespace wimi::client
