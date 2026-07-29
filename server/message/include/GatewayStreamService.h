#pragma once

#include "gateway_message.grpc.pb.h"

#include <grpcpp/support/server_callback.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace wimi::db {
struct SessionLease;
}

namespace wimi::rpc {

class GatewayStreamReactor;

class GatewayStreamService final
    : public gateway::GatewayMessageTransport::CallbackService {
 public:
  explicit GatewayStreamService(std::string messageNodeId,
                                bool requirePeerIdentity = false);

  grpc::ServerBidiReactor<gateway::GatewayToMessageFrame,
                          gateway::MessageToGatewayFrame> *
  Connect(grpc::CallbackServerContext *context) override;

  void Register(const std::string &gatewayId, const std::string &instanceId,
                GatewayStreamReactor *reactor);
  void Unregister(const std::string &gatewayId, const std::string &instanceId,
                  GatewayStreamReactor *reactor);
  bool Forward(const db::SessionLease &lease,
               gateway::ClientForwardEnvelope envelope);
  bool ForwardToUser(int64_t recipientUid,
                     gateway::ClientForwardEnvelope envelope,
                     const std::string &excludedDeviceId = {});
  bool Reply(const std::string &gatewayId, const std::string &instanceId,
             GatewayStreamReactor *reactor,
             gateway::MessageToGatewayFrame frame);
  void DrainAll(const std::string &reason);
  const std::string &MessageNodeId() const;
  bool RequirePeerIdentity() const;
  bool Draining() const;

 private:
  struct RegisteredStream {
    std::string instanceId;
    GatewayStreamReactor *reactor{nullptr};
  };

  std::string messageNodeId;
  bool requirePeerIdentity{false};
  std::atomic<bool> draining{false};
  std::mutex streamsMutex;
  std::unordered_map<std::string, RegisteredStream> streams;
};

}  // namespace wimi::rpc
