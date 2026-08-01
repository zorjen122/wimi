#pragma once

#include "gateway_message.grpc.pb.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

namespace wimi::connection
{

class MessageLinkManager;

class MessageLink final : public grpc::ClientBidiReactor<gateway::GatewayToMessageFrame, gateway::MessageToGatewayFrame>
{
  public:
    enum class EnqueueResult {
        Accepted,
        Stopped,
        QueueFull,
    };

    struct Node {
        std::string id;
        std::string host;
        unsigned short port{0};
    };

    MessageLink(Node node, std::string gatewayId, std::string instanceId, std::string token,
                MessageLinkManager& manager);

    void Start();
    void Stop();
    EnqueueResult Enqueue(gateway::GatewayToMessageFrame frame);
    void Heartbeat(uint64_t sequence);
    bool Healthy() const;
    bool Writable() const;
    std::size_t Inflight() const;
    void IncrementInflight();
    void DecrementInflight();
    const std::string& Id() const;
    const std::string& Token() const;
    void Drain();
    int64_t LastReadAt() const;

    void OnReadDone(bool ok) override;
    void OnWriteDone(bool ok) override;
    void OnDone(const grpc::Status& status) override;

  private:
    void ReleaseHold();

    Node node;
    std::string gatewayId;
    std::string instanceId;
    std::string token;
    MessageLinkManager& manager;
    grpc::ClientContext context;
    std::unique_ptr<gateway::GatewayMessageTransport::Stub> stub;
    gateway::MessageToGatewayFrame readFrame;
    gateway::GatewayToMessageFrame writeFrame;
    mutable std::mutex writeMutex;
    std::queue<gateway::GatewayToMessageFrame> writeQueue;
    bool writeInFlight{false};
    std::atomic<bool> externalHold{false};
    std::atomic<bool> stopped{false};
    std::atomic<bool> healthy{false};
    std::atomic<std::size_t> inflight{0};
    std::atomic<int64_t> lastReadAt{0};
};

} // namespace wimi::connection
