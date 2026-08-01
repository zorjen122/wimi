#include "MessageLink.h"

#include "Logger.h"
#include "MessageLinkManager.h"

#include <grpcpp/create_channel.h>
#include <chrono>
#include <utility>

namespace wimi::connection
{
namespace
{

constexpr std::size_t kMaxStreamQueue = 4096;

int64_t NowUnixMilliseconds()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

MessageLink::MessageLink(Node node, std::string gatewayId, std::string instanceId, MessageLinkManager& manager)
    : node(std::move(node)), gatewayId(std::move(gatewayId)), instanceId(std::move(instanceId)), manager(manager),
      lastReadAt(NowUnixMilliseconds())
{
    auto channel =
        grpc::CreateChannel(this->node.host + ":" + std::to_string(this->node.port), manager.messageCredentials);
    stub = gateway::GatewayMessageTransport::NewStub(channel);
}

void MessageLink::Start()
{
    LOG_INFO(netLogger,
             "Opening Gateway-Message stream, node: {}, endpoint: {}:{}, "
             "gateway_id: {}, instance_id: {}",
             node.id, node.host, node.port, gatewayId, instanceId);
    stub->async()->Connect(&context, this);
    AddHold();
    externalHold.store(true, std::memory_order_release);
    StartRead(&readFrame);

    gateway::GatewayToMessageFrame frame;
    auto* registration = frame.mutable_register_gateway();
    registration->set_protocol_version(1);
    registration->set_gateway_id(gatewayId);
    registration->set_instance_id(instanceId);
    registration->set_stream_epoch(static_cast<uint64_t>(NowUnixMilliseconds()));
    registration->set_capacity(kMaxStreamQueue);
    Enqueue(std::move(frame));
    StartCall();
}

void MessageLink::Stop()
{
    if (stopped.exchange(true))
        return;
    healthy.store(false, std::memory_order_release);
    context.TryCancel();
    ReleaseHold();
}

bool MessageLink::Enqueue(gateway::GatewayToMessageFrame frame)
{
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

void MessageLink::Heartbeat(uint64_t sequence)
{
    gateway::GatewayToMessageFrame frame;
    auto* heartbeat = frame.mutable_heartbeat();
    heartbeat->set_sent_at_unix_ms(NowUnixMilliseconds());
    heartbeat->set_sequence(sequence);
    Enqueue(std::move(frame));
}

bool MessageLink::Healthy() const { return healthy.load(std::memory_order_acquire); }

std::size_t MessageLink::Inflight() const { return inflight.load(std::memory_order_relaxed); }

void MessageLink::IncrementInflight() { inflight.fetch_add(1, std::memory_order_relaxed); }

void MessageLink::DecrementInflight()
{
    auto current = inflight.load(std::memory_order_relaxed);
    while (current > 0 && !inflight.compare_exchange_weak(current, current - 1, std::memory_order_relaxed)) {
    }
}

const std::string& MessageLink::Id() const { return node.id; }

void MessageLink::Drain()
{
    healthy.store(false, std::memory_order_release);
    Stop();
}

int64_t MessageLink::LastReadAt() const { return lastReadAt.load(std::memory_order_relaxed); }

void MessageLink::OnReadDone(bool ok)
{
    if (!ok) {
        LOG_WARN(netLogger, "Gateway-Message read side closed, node: {}, gateway_id: {}", node.id, gatewayId);
        stopped.store(true, std::memory_order_release);
        healthy.store(false, std::memory_order_release);
        ReleaseHold();
        return;
    }
    lastReadAt.store(NowUnixMilliseconds(), std::memory_order_relaxed);
    if (readFrame.has_register_result())
        healthy.store(readFrame.register_result().accepted(), std::memory_order_release);
    manager.OnFrame(node.id, readFrame);
    readFrame.Clear();
    StartRead(&readFrame);
}

void MessageLink::OnWriteDone(bool ok)
{
    if (!ok) {
        LOG_WARN(netLogger, "Gateway-Message write side closed, node: {}, gateway_id: {}", node.id, gatewayId);
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

void MessageLink::OnDone(const grpc::Status& status)
{
    stopped.store(true, std::memory_order_release);
    healthy.store(false, std::memory_order_release);
    LOG_WARN(netLogger, "Gateway-Message stream closed, node: {}, status: {}", node.id, status.error_message());
    manager.OnLinkDone(node.id, this);
}

void MessageLink::ReleaseHold()
{
    bool expected = true;
    if (externalHold.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
        RemoveHold();
}

} // namespace wimi::connection
