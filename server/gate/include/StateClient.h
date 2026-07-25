#pragma once
#include "state.grpc.pb.h"
#include "state.pb.h"
#include <grpcpp/grpcpp.h>

#include "Const.h"
#include "RpcPool.h"

using ::grpc::Channel;
using ::grpc::ClientContext;
using ::grpc::Status;

namespace wimi::rpc {
using ::state::ConnectUser;

using ::state::ConnectUserRsp;
using ::state::StateService;

class ServerNode {
 public:
  ServerNode() = default;
  ServerNode(std::string ip, int port, std::string nodeId = {})
      : ip(std::move(ip)), port(port), nodeId(std::move(nodeId)) {}

  bool empty() {
    return ip.empty() || port == 0;
  }
  std::string ip;
  unsigned short port;
  std::string nodeId;
};

class StateClient : public Singleton<StateClient> {
 public:
  ServerNode PickConnectionGateway(int uid);
  StateClient();

 private:
  std::unique_ptr<RpcPool<StateService>> rpcPool = nullptr;
};

};  // namespace wimi::rpc
