#include "MessageLink.h"

#include "Configer.h"
#include "Const.h"
#include "GrpcSecurity.h"
#include "Logger.h"
#include "TcpMessageCodec.h"
#include "gateway_message.grpc.pb.h"
#include "state.grpc.pb.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <random>
#include <utility>

namespace wimi::connection {
namespace asio = boost::asio;
namespace {

constexpr std::size_t kMaxStreamQueue = 4096;
constexpr auto kReconnectBase = std::chrono::milliseconds(200);
constexpr auto kReconnectMaximum = std::chrono::seconds(10);

int64_t NowUnixMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool CanRetryOnAnotherMessageNode(uint32_t serviceId) {
  switch (serviceId) {
    case ID_TEXT_SEND_REQ:
    case ID_GROUP_TEXT_SEND_REQ:
    case ID_PULL_FRIEND_LIST_REQ:
    case ID_PULL_FRIEND_APPLY_LIST_REQ:
    case ID_PULL_SESSION_MESSAGE_LIST_REQ:
    case ID_ACK:
      return true;
    default:
      return false;
  }
}

}  // namespace

class MessageLink final
    : public grpc::ClientBidiReactor<gateway::GatewayToMessageFrame,
                                     gateway::MessageToGatewayFrame> {
 public:
  MessageLink(MessageLinkManager::Node node, std::string gatewayId,
              std::string instanceId, MessageLinkManager &manager)
      : node(std::move(node)),
        gatewayId(std::move(gatewayId)),
        instanceId(std::move(instanceId)),
        manager(manager) {
    auto channel = grpc::CreateChannel(
        this->node.host + ":" + std::to_string(this->node.port),
        manager.messageCredentials);
    stub = gateway::GatewayMessageTransport::NewStub(channel);
  }

  void Start() {
    LOG_INFO(netLogger,
             "Opening Gateway-Message stream, node: {}, endpoint: {}:{}, "
             "gateway_id: {}, instance_id: {}",
             node.id, node.host, node.port, gatewayId, instanceId);
    stub->async()->Connect(&context, this);
    AddHold();
    externalHold.store(true, std::memory_order_release);
    StartRead(&readFrame);

    gateway::GatewayToMessageFrame frame;
    auto *registration = frame.mutable_register_gateway();
    registration->set_protocol_version(1);
    registration->set_gateway_id(gatewayId);
    registration->set_instance_id(instanceId);
    registration->set_stream_epoch(
        static_cast<uint64_t>(NowUnixMilliseconds()));
    registration->set_capacity(kMaxStreamQueue);
    Enqueue(std::move(frame));
    StartCall();
  }

  void Stop() {
    if (stopped.exchange(true))
      return;
    healthy.store(false, std::memory_order_release);
    context.TryCancel();
    ReleaseHold();
  }

  bool Enqueue(gateway::GatewayToMessageFrame frame) {
    bool startWrite = false;
    {
      std::lock_guard<std::mutex> lock(writeMutex);
      if (stopped || writeQueue.size() >= kMaxStreamQueue)
        return false;
      writeQueue.push_back(std::move(frame));
      if (!writeInFlight) {
        writeInFlight = true;
        writeFrame = std::move(writeQueue.front());
        writeQueue.pop_front();
        startWrite = true;
      }
    }
    if (startWrite)
      StartWrite(&writeFrame);
    return true;
  }

  void Heartbeat(uint64_t sequence) {
    gateway::GatewayToMessageFrame frame;
    auto *heartbeat = frame.mutable_heartbeat();
    heartbeat->set_sent_at_unix_ms(NowUnixMilliseconds());
    heartbeat->set_sequence(sequence);
    Enqueue(std::move(frame));
  }

  bool Healthy() const {
    return healthy.load(std::memory_order_acquire);
  }

  std::size_t Inflight() const {
    return inflight.load(std::memory_order_relaxed);
  }

  void IncrementInflight() {
    inflight.fetch_add(1, std::memory_order_relaxed);
  }

  void DecrementInflight() {
    auto current = inflight.load(std::memory_order_relaxed);
    while (current > 0 &&
           !inflight.compare_exchange_weak(current, current - 1,
                                           std::memory_order_relaxed)) {
    }
  }

  const std::string &Id() const {
    return node.id;
  }

  void Drain() {
    healthy.store(false, std::memory_order_release);
    Stop();
  }

  int64_t LastReadAt() const {
    return lastReadAt.load(std::memory_order_relaxed);
  }

  void OnReadDone(bool ok) override {
    if (!ok) {
      LOG_WARN(netLogger,
               "Gateway-Message read side closed, node: {}, gateway_id: {}",
               node.id, gatewayId);
      stopped.store(true, std::memory_order_release);
      healthy.store(false, std::memory_order_release);
      ReleaseHold();
      return;
    }
    lastReadAt.store(NowUnixMilliseconds(), std::memory_order_relaxed);
    if (readFrame.has_register_result())
      healthy.store(readFrame.register_result().accepted(),
                    std::memory_order_release);
    manager.OnFrame(node.id, readFrame);
    readFrame.Clear();
    StartRead(&readFrame);
  }

  void OnWriteDone(bool ok) override {
    if (!ok) {
      LOG_WARN(netLogger,
               "Gateway-Message write side closed, node: {}, gateway_id: {}",
               node.id, gatewayId);
      stopped.store(true, std::memory_order_release);
      healthy.store(false, std::memory_order_release);
      ReleaseHold();
      return;
    }
    bool startWrite = false;
    {
      std::lock_guard<std::mutex> lock(writeMutex);
      if (!writeQueue.empty()) {
        writeFrame = std::move(writeQueue.front());
        writeQueue.pop_front();
        startWrite = true;
      } else {
        writeInFlight = false;
      }
    }
    if (startWrite)
      StartWrite(&writeFrame);
  }

  void OnDone(const grpc::Status &status) override {
    stopped.store(true, std::memory_order_release);
    healthy.store(false, std::memory_order_release);
    LOG_WARN(netLogger, "Gateway-Message stream closed, node: {}, status: {}",
             node.id, status.error_message());

    // 当 gRPC 流最终 OnDone，manager 会在 OnLinkDone 中移除旧 link、
    // 尝试重试 pending 命令，并用指数退避加 jitter 重连
    manager.OnLinkDone(node.id, this);
  }

 private:
  void ReleaseHold() {
    bool expected = true;
    if (externalHold.compare_exchange_strong(expected, false,
                                             std::memory_order_acq_rel))
      RemoveHold();
  }

  MessageLinkManager::Node node;
  std::string gatewayId;
  std::string instanceId;
  MessageLinkManager &manager;
  grpc::ClientContext context;
  std::unique_ptr<gateway::GatewayMessageTransport::Stub> stub;
  gateway::MessageToGatewayFrame readFrame;
  gateway::GatewayToMessageFrame writeFrame;
  std::mutex writeMutex;
  std::deque<gateway::GatewayToMessageFrame> writeQueue;
  bool writeInFlight{false};
  std::atomic<bool> externalHold{false};
  std::atomic<bool> stopped{false};
  std::atomic<bool> healthy{false};
  std::atomic<std::size_t> inflight{0};
  std::atomic<int64_t> lastReadAt{NowUnixMilliseconds()};
};

MessageLinkManager::MessageLinkManager(asio::io_context &ioContext,
                                       asio::thread_pool &controlPool,
                                       std::string gatewayId,
                                       std::string instanceId)
    : ioContext(ioContext),
      controlPool(controlPool),
      gatewayId(std::move(gatewayId)),
      instanceId(std::move(instanceId)) {
  auto config = Configer::getNode("server");
  messageCredentials = BuildChannelCredentials(LoadGrpcSecurityConfig(config));
  if (config["stateRPC"]) {
    stateAddress = config["stateRPC"]["host"].as<std::string>() + ":" +
                   config["stateRPC"]["port"].as<std::string>();
  }
}

MessageLinkManager::~MessageLinkManager() {
  Stop();
}

void MessageLinkManager::Start() {
  TopologySnapshot initial;
  initial.changed = true;
  initial.version = 0;
  initial.nodes = LoadConfiguredNodes();
  ApplyTopology(initial);
  asio::co_spawn(ioContext, TopologyLoop(), asio::detached);
}

void MessageLinkManager::Stop() {
  if (stopping.exchange(true))
    return;
  std::vector<std::shared_ptr<MessageLink>> current;
  {
    std::lock_guard<std::mutex> lock(linksMutex);
    for (auto &[_, link] : links)
      current.push_back(link);
  }
  for (auto &link : current)
    link->Stop();

  std::vector<PendingCommand> abandoned;
  {
    std::lock_guard<std::mutex> lock(pendingMutex);
    for (auto &[_, command] : pending)
      abandoned.push_back(std::move(command));
    pending.clear();
  }
  for (auto &command : abandoned) {
    if (command.deadlineTimer)
      command.deadlineTimer->cancel();
    if (!command.callback)
      continue;
    gateway::CommandResult result;
    result.set_request_id(command.command.request_id());
    result.set_response_service_id(
        __getServiceResponseId(ServiceID(command.command.service_id())));
    result.set_error(ErrorCodes::DependencyUnavailable);
    result.set_retryable(true);
    result.set_packet(SerializeTcpPacket(MakeErrorPacket(
        ErrorCodes::DependencyUnavailable, "message links are stopping")));
    command.callback(result);
  }
}

bool MessageLinkManager::Ready() const {
  return HealthyLinkCount() > 0;
}

std::size_t MessageLinkManager::HealthyLinkCount() const {
  std::lock_guard<std::mutex> lock(linksMutex);
  return std::count_if(links.begin(), links.end(),
                       [](const auto &item) { return item.second->Healthy(); });
}

bool MessageLinkManager::Forward(gateway::CommandEnvelope command,
                                 CommandCallback callback) {
  const int64_t now = NowUnixMilliseconds();
  if (command.deadline_unix_ms() <= now)
    return false;
  auto link = SelectLink(command.conversation_id());
  if (!link)
    return false;

  const std::string requestId = command.request_id();
  auto deadlineTimer = std::make_shared<asio::steady_timer>(ioContext);
  deadlineTimer->expires_after(
      std::chrono::milliseconds(command.deadline_unix_ms() - now));
  {
    std::lock_guard<std::mutex> lock(pendingMutex);
    if (pending.contains(requestId))
      return false;
    pending.emplace(requestId, PendingCommand{command, std::move(callback),
                                              link->Id(), 0, deadlineTimer});
  }
  deadlineTimer->async_wait(
      [this, requestId](const boost::system::error_code &error) {
        if (!error)
          ExpirePending(requestId);
      });

  gateway::GatewayToMessageFrame frame;
  *frame.mutable_command() = std::move(command);
  if (!link->Enqueue(std::move(frame))) {
    PendingCommand rejected;
    {
      std::lock_guard<std::mutex> lock(pendingMutex);
      auto found = pending.find(requestId);
      if (found != pending.end()) {
        rejected = std::move(found->second);
        pending.erase(found);
      }
    }
    deadlineTimer->cancel();
    if (rejected.callback) {
      gateway::CommandResult result;
      result.set_request_id(requestId);
      result.set_response_service_id(
          __getServiceResponseId(ServiceID(rejected.command.service_id())));
      result.set_error(ErrorCodes::ResourceExhausted);
      result.set_retryable(true);
      result.set_packet(SerializeTcpPacket(MakeErrorPacket(
          ErrorCodes::ResourceExhausted, "message stream queue is full")));
      rejected.callback(result);
    }
    return true;
  }
  link->IncrementInflight();
  return true;
}

void MessageLinkManager::SetClientForwardHandler(ClientForwardHandler handler) {
  clientForwardHandler = std::move(handler);
}

asio::awaitable<void> MessageLinkManager::TopologyLoop() {
  asio::steady_timer timer(ioContext);
  uint64_t heartbeatSequence = 0;

  // 如果配置了 stateRPC，循环会定期调用 StateService 拉最新 message
  // 节点拓扑；否则就使用本地配置。
  while (!stopping.load(std::memory_order_acquire)) {
    if (!stateAddress.empty()) {
      auto snapshot = co_await asio::co_spawn(
          controlPool,
          [this]() -> asio::awaitable<TopologySnapshot> {
            co_return FetchTopology();
          },
          asio::use_awaitable);
      if (snapshot.changed)
        ApplyTopology(snapshot);
    }

    std::vector<std::shared_ptr<MessageLink>> current;
    {
      std::lock_guard<std::mutex> lock(linksMutex);
      for (auto &[_, link] : links)
        current.push_back(link);
    }

    // 每 5 秒还会做两件事：给健康 link 发 heartbeat；
    // 如果某条 link 超过 30 秒没有读到任何帧，就主动 stop 它。

    const int64_t now = NowUnixMilliseconds();
    for (auto &link : current) {
      if (link->Healthy())
        link->Heartbeat(++heartbeatSequence);
      if (now - link->LastReadAt() > 30000)
        link->Stop();
    }

    timer.expires_after(std::chrono::seconds(5));
    boost::system::error_code ec;
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
    if (ec == asio::error::operation_aborted)
      break;
  }
}

MessageLinkManager::TopologySnapshot MessageLinkManager::FetchTopology() {
  TopologySnapshot snapshot;
  if (stateAddress.empty())
    return snapshot;
  auto channel =
      grpc::CreateChannel(stateAddress, grpc::InsecureChannelCredentials());
  auto stub = state::StateService::NewStub(channel);
  state::TopologyRequest request;
  request.set_known_version(topologyVersion.load(std::memory_order_acquire));
  state::MessageTopology response;
  grpc::ClientContext context;
  context.set_deadline(std::chrono::system_clock::now() +
                       std::chrono::seconds(2));
  auto status = stub->ListMessageNodes(&context, request, &response);
  if (!status.ok()) {
    LOG_WARN(netLogger, "ListMessageNodes failed: {}", status.error_message());
    return snapshot;
  }
  snapshot.version = response.topology_version();
  snapshot.changed = snapshot.version != request.known_version();
  for (const auto &source : response.nodes()) {
    if (source.status() != "active" || source.host().empty() ||
        source.port() <= 0)
      continue;
    snapshot.nodes.push_back(Node{source.node_id(), source.host(),
                                  static_cast<unsigned short>(source.port())});
  }
  return snapshot;
}

std::vector<MessageLinkManager::Node> MessageLinkManager::LoadConfiguredNodes()
    const {
  std::vector<Node> nodes;
  auto config = Configer::getNode("server");
  auto message = config["message"];
  if (!message || !message["message-total"])
    return nodes;
  const int total = message["message-total"].as<int>();
  for (int i = 1; i <= total; ++i) {
    auto source = message["m" + std::to_string(i)];
    if (!source)
      continue;
    nodes.push_back(Node{source["name"].as<std::string>(),
                         source["host"].as<std::string>(),
                         source["streamPort"].as<unsigned short>()});
  }
  return nodes;
}

void MessageLinkManager::ApplyTopology(const TopologySnapshot &snapshot) {
  if (snapshot.changed && snapshot.version != 0)
    topologyVersion.store(snapshot.version, std::memory_order_release);

  std::unordered_map<std::string, Node> desired;
  for (const auto &node : snapshot.nodes)
    desired[node.id] = node;

  std::vector<std::shared_ptr<MessageLink>> removed;
  {
    std::lock_guard<std::mutex> lock(linksMutex);
    for (auto current = links.begin(); current != links.end();) {
      const auto wanted = desired.find(current->first);
      const auto configured = configuredNodes.find(current->first);
      const bool endpointChanged =
          wanted != desired.end() && configured != configuredNodes.end() &&
          (wanted->second.host != configured->second.host ||
           wanted->second.port != configured->second.port);
      if (wanted == desired.end() || endpointChanged) {
        removed.push_back(current->second);
        RetireLink(current->second);
        reconnectAttempts.erase(current->first);
        current = links.erase(current);
      } else {
        ++current;
      }
    }
    configuredNodes = desired;
  }
  for (auto &link : removed)
    link->Stop();

  for (const auto &node : snapshot.nodes) {
    bool start = false;
    {
      std::lock_guard<std::mutex> lock(linksMutex);
      start = !links.contains(node.id);
    }
    if (start)
      StartLink(node);
  }
}

void MessageLinkManager::StartLink(const Node &node) {
  auto link = std::make_shared<MessageLink>(node, gatewayId, instanceId, *this);
  {
    std::lock_guard<std::mutex> lock(linksMutex);
    auto found = links.find(node.id);
    if (found != links.end()) {
      auto old = found->second;
      RetireLink(old);
      old->Stop();
    }
    links[node.id] = link;
  }
  link->Start();
}

void MessageLinkManager::OnFrame(const std::string &nodeId,
                                 const gateway::MessageToGatewayFrame &frame) {
  // CommandResult 通过 request_id 与在途命令配对，只完成一次回调并取消
  // deadline。
  if (frame.has_command_result()) {
    PendingCommand command;
    bool found = false;
    {
      std::lock_guard<std::mutex> lock(pendingMutex);
      auto pendingIt = pending.find(frame.command_result().request_id());
      if (pendingIt != pending.end()) {
        command = std::move(pendingIt->second);
        pending.erase(pendingIt);
        found = true;
      }
    }
    if (found && command.deadlineTimer)
      command.deadlineTimer->cancel();
    {
      std::lock_guard<std::mutex> lock(linksMutex);
      auto link = links.find(nodeId);
      if (link != links.end())
        link->second->DecrementInflight();
    }
    if (found && command.callback) {
      LOG_DEBUG(businessLogger,
                "Gateway received Message command result, node: {}, "
                "request_id: {}, response_service_id: {}, error: {}, "
                "retryable: {}",
                nodeId, frame.command_result().request_id(),
                frame.command_result().response_service_id(),
                frame.command_result().error(),
                frame.command_result().retryable());
      command.callback(frame.command_result());
    } else {
      LOG_WARN(businessLogger,
               "Gateway ignored unmatched Message command result, node: {}, "
               "request_id: {}, response_service_id: {}",
               nodeId, frame.command_result().request_id(),
               frame.command_result().response_service_id());
    }
    return;
  }

  // RegisterResult 是流进入 healthy 的门禁；拒绝结果保留 unhealthy
  // 并等待流关闭重连。
  if (frame.has_register_result()) {
    if (frame.register_result().accepted()) {
      {
        std::lock_guard<std::mutex> lock(linksMutex);
        reconnectAttempts[nodeId] = 0;
      }
      LOG_INFO(netLogger,
               "Gateway-Message registration accepted, node: {}, "
               "message_node_id: {}, stream_epoch: {}",
               nodeId, frame.register_result().message_node_id(),
               frame.register_result().stream_epoch());
    } else {
      LOG_ERROR(netLogger,
                "Gateway-Message registration rejected, node: {}, "
                "message_node_id: {}, stream_epoch: {}, reason: {}",
                nodeId, frame.register_result().message_node_id(),
                frame.register_result().stream_epoch(),
                frame.register_result().reason());
    }
    return;
  }

  // HeartbeatAck 的到达时间已在 OnReadDone 更新；这里只记录序号和往返延迟。
  if (frame.has_heartbeat_ack()) {
    LOG_TRACE(
        netLogger,
        "Gateway received Message heartbeat ACK, node: {}, sequence: {}, "
        "round_trip_ms: {}",
        nodeId, frame.heartbeat_ack().sequence(),
        std::max<int64_t>(0, NowUnixMilliseconds() -
                                 frame.heartbeat_ack().sent_at_unix_ms()));
    return;
  }

  // DrainNotice 立即把节点移出健康集合，禁止新命令继续路由到正在下线的流。
  if (frame.has_drain_notice()) {
    LOG_WARN(netLogger,
             "Gateway received Message drain notice, node: {}, "
             "message_node_id: {}, reason: {}",
             nodeId, frame.drain_notice().message_node_id(),
             frame.drain_notice().reason());
    std::shared_ptr<MessageLink> link;
    {
      std::lock_guard<std::mutex> lock(linksMutex);
      auto found = links.find(nodeId);
      if (found != links.end())
        link = found->second;
    }
    if (link)
      link->Drain();
    return;
  }

  // ClientForward 先交给本地 SessionRegistry 做 generation 校验和物理推送，再沿
  // 原流返回 ClientForwardAck；业务消息已持久化，因此离线/背压不会回滚
  // ACCEPTED。
  if (frame.has_client_forward()) {
    LOG_DEBUG(
        businessLogger,
        "Gateway handling Message client forward, node: {}, forward_id: {}, "
        "recipient_uid: {}, message_id: {}, conversation_id: {}, "
        "conversation_seq: {}",
        nodeId, frame.client_forward().forward_id(),
        frame.client_forward().recipient_uid(),
        frame.client_forward().message_id(),
        frame.client_forward().conversation_id(),
        frame.client_forward().conversation_seq());
    gateway::ClientForwardStatus status =
        gateway::CLIENT_FORWARD_STATUS_OFFLINE;
    if (clientForwardHandler) {
      status = clientForwardHandler(frame.client_forward());
    } else {
      LOG_ERROR(businessLogger,
                "Gateway client-forward handler is not installed, node: {}, "
                "forward_id: {}",
                nodeId, frame.client_forward().forward_id());
    }
    std::shared_ptr<MessageLink> link;
    {
      std::lock_guard<std::mutex> lock(linksMutex);
      auto found = links.find(nodeId);
      if (found != links.end())
        link = found->second;
    }
    if (link) {
      gateway::GatewayToMessageFrame ackFrame;
      auto *ack = ackFrame.mutable_client_forward_ack();
      ack->set_forward_id(frame.client_forward().forward_id());
      ack->set_status(status);
      ack->set_gateway_id(gatewayId);
      ack->set_instance_id(instanceId);
      ack->set_recipient_device_id(
          frame.client_forward().recipient_device_id());
      if (!link->Enqueue(std::move(ackFrame))) {
        LOG_WARN(netLogger,
                 "Gateway failed to enqueue client-forward ACK, node: {}, "
                 "forward_id: {}, status: {}",
                 nodeId, frame.client_forward().forward_id(),
                 static_cast<int>(status));
      } else {
        LOG_DEBUG(
            businessLogger,
            "Gateway enqueued client-forward ACK, node: {}, forward_id: {}, "
            "status: {}",
            nodeId, frame.client_forward().forward_id(),
            static_cast<int>(status));
      }
    } else {
      LOG_WARN(
          netLogger,
          "Gateway cannot return client-forward ACK because link is absent, "
          "node: {}, forward_id: {}, status: {}",
          nodeId, frame.client_forward().forward_id(),
          static_cast<int>(status));
    }
    return;
  }

  // oneof 理论上只会命中上述分支；空 frame 作为协议异常保留可观测性。
  LOG_WARN(netLogger, "Gateway received empty Message frame, node: {}", nodeId);
}

void MessageLinkManager::OnLinkDone(const std::string &nodeId,
                                    MessageLink *source) {
  asio::post(ioContext, [this, nodeId, source]() {
    if (stopping.load(std::memory_order_acquire))
      return;
    Node node;
    {
      std::lock_guard<std::mutex> lock(linksMutex);
      auto found = links.find(nodeId);
      if (found == links.end() || found->second.get() != source)
        return;
      RetireLink(found->second);
      links.erase(found);
      auto configured = configuredNodes.find(nodeId);
      if (configured == configuredNodes.end())
        return;
      node = configured->second;
    }
    RetryPending(nodeId);
    unsigned int attempt = 0;
    {
      std::lock_guard<std::mutex> lock(linksMutex);
      attempt = reconnectAttempts[nodeId]++;
    }
    const auto exponent = std::min(attempt, 6U);
    auto delay = kReconnectBase * (1U << exponent);
    delay =
        std::min(std::chrono::duration_cast<std::chrono::milliseconds>(delay),
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     kReconnectMaximum));
    thread_local std::mt19937 random(std::random_device{}());
    std::uniform_int_distribution<int64_t> jitter(
        0, std::max<int64_t>(1, delay.count() / 4));
    delay += std::chrono::milliseconds(jitter(random));
    auto timer = std::make_shared<asio::steady_timer>(ioContext);
    timer->expires_after(delay);
    timer->async_wait([this, node, timer](const boost::system::error_code &ec) {
      if (ec || stopping.load(std::memory_order_acquire))
        return;
      {
        std::lock_guard<std::mutex> lock(linksMutex);
        auto configured = configuredNodes.find(node.id);
        if (configured == configuredNodes.end() || links.contains(node.id) ||
            configured->second.host != node.host ||
            configured->second.port != node.port)
          return;
      }
      StartLink(node);
    });
  });
}

void MessageLinkManager::ExpirePending(const std::string &requestId) {
  PendingCommand expired;
  {
    std::lock_guard<std::mutex> lock(pendingMutex);
    auto found = pending.find(requestId);
    if (found == pending.end())
      return;
    expired = std::move(found->second);
    pending.erase(found);
  }
  if (!expired.callback)
    return;
  gateway::CommandResult result;
  result.set_request_id(requestId);
  result.set_response_service_id(
      __getServiceResponseId(ServiceID(expired.command.service_id())));
  result.set_error(ErrorCodes::DeadlineExceeded);
  result.set_retryable(true);
  result.set_packet(SerializeTcpPacket(MakeErrorPacket(
      ErrorCodes::DeadlineExceeded, "message command deadline exceeded")));
  expired.callback(result);
}

void MessageLinkManager::RetireLink(std::shared_ptr<MessageLink> link) {
  retiredLinks.push_back(link);
  auto timer = std::make_shared<asio::steady_timer>(ioContext);
  timer->expires_after(std::chrono::seconds(1));
  timer->async_wait(
      [this, link = std::move(link), timer](const boost::system::error_code &) {
        std::lock_guard<std::mutex> lock(linksMutex);
        std::erase(retiredLinks, link);
      });
}

void MessageLinkManager::RetryPending(const std::string &failedNodeId) {
  std::vector<std::string> requestIds;
  {
    std::lock_guard<std::mutex> lock(pendingMutex);
    for (const auto &[requestId, command] : pending) {
      if (command.linkId == failedNodeId)
        requestIds.push_back(requestId);
    }
  }

  for (const auto &requestId : requestIds) {
    PendingCommand failed;
    {
      std::lock_guard<std::mutex> lock(pendingMutex);
      auto found = pending.find(requestId);
      if (found == pending.end() || found->second.attempts >= 1 ||
          !CanRetryOnAnotherMessageNode(found->second.command.service_id()))
        continue;
      found->second.attempts++;
      failed = found->second;
    }
    if (failed.command.deadline_unix_ms() > 0 &&
        failed.command.deadline_unix_ms() <= NowUnixMilliseconds())
      continue;
    auto retry = SelectLink(failed.command.conversation_id(), failedNodeId);
    if (!retry)
      continue;
    gateway::GatewayToMessageFrame frame;
    *frame.mutable_command() = failed.command;
    if (retry->Enqueue(std::move(frame))) {
      retry->IncrementInflight();
      std::lock_guard<std::mutex> lock(pendingMutex);
      auto found = pending.find(requestId);
      if (found != pending.end())
        found->second.linkId = retry->Id();
    }
  }
}

std::shared_ptr<MessageLink> MessageLinkManager::SelectLink(
    int64_t conversationId, const std::string &excluded) {
  std::vector<std::shared_ptr<MessageLink>> healthy;
  {
    std::lock_guard<std::mutex> lock(linksMutex);
    for (const auto &[nodeId, link] : links) {
      if (nodeId != excluded && link->Healthy())
        healthy.push_back(link);
    }
  }
  if (healthy.empty())
    return {};

  if (conversationId > 0) {
    std::shared_ptr<MessageLink> selected;
    std::size_t best = 0;
    for (const auto &link : healthy) {
      auto score = std::hash<std::string>{}(std::to_string(conversationId) +
                                            ":" + link->Id());
      if (!selected || score > best) {
        best = score;
        selected = link;
      }
    }
    return selected;
  }

  return *std::min_element(healthy.begin(), healthy.end(),
                           [](const auto &left, const auto &right) {
                             return left->Inflight() < right->Inflight();
                           });
}

}  // namespace wimi::connection
