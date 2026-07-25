#include "SessionRegistry.h"

#include "GatewaySession.h"
#include "Logger.h"
#include "TcpMessageCodec.h"

#include <utility>

namespace wimi::connection {

SessionRegistry::SessionRegistry(std::string gatewayId, std::string instanceId,
                                 long leaseTtlSeconds)
    : gatewayId(std::move(gatewayId)),
      instanceId(std::move(instanceId)),
      leaseTtlSeconds(leaseTtlSeconds) {}

db::SessionLease SessionRegistry::Bind(
    int64_t uid, const std::shared_ptr<GatewaySession> &session) {
  db::SessionLease lease;
  lease.gatewayId = gatewayId;
  lease.instanceId = instanceId;
  lease.connectionId = session->ConnectionId();
  lease.generation = db::RedisDao::GetInstance()->bindSessionLease(
      uid, gatewayId, instanceId, lease.connectionId, leaseTtlSeconds);
  if (lease.generation <= 0)
    return {};

  // rebind 会话，如果旧的会话存在，目前的规则是直接关闭旧的会话（没有明确通知）
  std::shared_ptr<GatewaySession> oldSession;
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto found = sessions.find(uid);
    if (found != sessions.end())
      oldSession = found->second.session.lock();
    sessions[uid] = LocalSession{session, lease};
  }
  if (oldSession && oldSession != session)
    oldSession->Close();
  return lease;
}

bool SessionRegistry::Refresh(int64_t uid, const db::SessionLease &lease) {
  return db::RedisDao::GetInstance()->refreshSessionLease(uid, lease,
                                                          leaseTtlSeconds);
}

void SessionRegistry::Remove(int64_t uid,
                             const std::shared_ptr<GatewaySession> &session,
                             const db::SessionLease &lease) {
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto found = sessions.find(uid);
    if (found == sessions.end())
      return;
    auto current = found->second.session.lock();
    if (current && current != session)
      return;
    sessions.erase(found);
  }
  db::RedisDao::GetInstance()->clearSessionLease(uid, lease);
}

gateway::ClientForwardStatus SessionRegistry::Forward(
    const gateway::ClientForwardEnvelope &forward) {
  std::shared_ptr<GatewaySession> session;
  db::SessionLease lease;
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto found = sessions.find(forward.recipient_uid());
    if (found == sessions.end())
      return gateway::CLIENT_FORWARD_STATUS_OFFLINE;
    session = found->second.session.lock();
    lease = found->second.lease;
  }

  if (!session)
    return gateway::CLIENT_FORWARD_STATUS_OFFLINE;

  /*
    Message Core 查询在线 lease 后，把它当时看到的连接身份写入
    ClientForwardEnvelope。Gateway 在真正写 socket 前，要求本地当前 session 的
    connectionId 和 generation 都完全一致；不一致就返回 STALE_ROUTE，不投递。
    未来可以考虑投递，不然导致了一个用户的消息（如通知）无法及时收到，只因为它的连接身份变了。
    目前一般来说，对于可持久化消息是不会丢的，它存放在 Mysql 存储表中。
  */
  if (lease.connectionId != forward.expected_connection_id() ||
      lease.generation != forward.expected_connection_generation())
    return gateway::CLIENT_FORWARD_STATUS_STALE_ROUTE;
  TcpPacket packet;
  const bool hasTransportSequence = ParseTcpPacket(forward.packet(), packet) &&
                                    packet.has_seq() && packet.seq() > 0;
  const bool queued =
      hasTransportSequence
          ? session->SendReliable(forward.packet(), forward.protocol_id(),
                                  packet.seq())
          : session->SendRaw(forward.packet(), forward.protocol_id());
  if (!queued)
    return gateway::CLIENT_FORWARD_STATUS_BACKPRESSURED;
  return gateway::CLIENT_FORWARD_STATUS_QUEUED;
}

const std::string &SessionRegistry::GatewayId() const {
  return gatewayId;
}

const std::string &SessionRegistry::InstanceId() const {
  return instanceId;
}

}  // namespace wimi::connection
