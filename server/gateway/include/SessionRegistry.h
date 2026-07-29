#pragma once

#include "Redis.h"
#include "gateway_message.pb.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace wimi::connection {

class GatewaySession;

class SessionRegistry {
 public:
  SessionRegistry(std::string gatewayId, std::string instanceId,
                  long leaseTtlSeconds = 60);

  db::SessionLease Bind(int64_t uid, const std::string &deviceId,
                        const std::shared_ptr<GatewaySession> &session);
  bool Refresh(int64_t uid, const std::string &deviceId,
               const db::SessionLease &lease);
  void Remove(int64_t uid, const std::string &deviceId,
              const std::shared_ptr<GatewaySession> &session,
              const db::SessionLease &lease);
  gateway::ClientForwardStatus Forward(
      const gateway::ClientForwardEnvelope &forward);

  const std::string &GatewayId() const;
  const std::string &InstanceId() const;

 private:
  struct LocalSession {
    std::weak_ptr<GatewaySession> session;
    db::SessionLease lease;
  };

  std::string gatewayId;
  std::string instanceId;
  long leaseTtlSeconds;
  std::mutex mutex;
  std::unordered_map<int64_t, std::unordered_map<std::string, LocalSession>>
      sessions;
};

}  // namespace wimi::connection
