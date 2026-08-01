#include "ports/IPlatformServices.h"

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
#include "platform/linux/LinuxDesktopServices.h"
#endif

namespace wimi::client
{
namespace
{

class UnsupportedPlatformServices final : public IPlatformServices
{
  public:
    QString PlatformName() const override { return QStringLiteral("unsupported"); }

    bool DesktopNotificationsAvailable() const override { return false; }

    bool ShowDesktopNotification(const QString&, const QString&) override { return false; }
};

} // namespace

std::unique_ptr<IPlatformServices> CreatePlatformServices()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return std::make_unique<LinuxDesktopServices>();
#else
    return std::make_unique<UnsupportedPlatformServices>();
#endif
}

} // namespace wimi::client
