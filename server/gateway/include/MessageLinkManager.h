#pragma once

#include "MessageLink.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>
#include <grpcpp/security/credentials.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace wimi::connection
{

class MessageLinkManager
{
  public:
    using CommandCallback = std::function<void(const gateway::CommandResult& result)>;
    using ClientForwardHandler =
        std::function<gateway::ClientForwardStatus(const gateway::ClientForwardEnvelope& forward)>;
    using Node = MessageLink::Node;

    MessageLinkManager(boost::asio::io_context& ioContext, boost::asio::thread_pool& controlPool, std::string gatewayId,
                       std::string instanceId);
    ~MessageLinkManager();

    void Start();
    void Stop();
    bool Ready() const;
    std::size_t HealthyLinkCount() const;
    bool Forward(gateway::CommandEnvelope command, CommandCallback callback);
    void SetClientForwardHandler(ClientForwardHandler handler);

  private:
    friend class MessageLink;

    struct PendingCommand {
        gateway::CommandEnvelope command;
        CommandCallback callback;
        std::string linkId;
        unsigned int attempts{0};
        std::shared_ptr<boost::asio::steady_timer> deadlineTimer;
    };
    struct TopologySnapshot {
        std::uint64_t version{0};
        bool changed{false};
        std::vector<Node> nodes;
    };

    boost::asio::awaitable<void> TopologyLoop();
    TopologySnapshot FetchTopology();
    std::vector<Node> LoadConfiguredNodes() const;
    void ApplyTopology(const TopologySnapshot& snapshot);
    void StartLink(const Node& node);
    void OnFrame(const std::string& nodeId, const gateway::MessageToGatewayFrame& frame);
    void OnLinkDone(const std::string& nodeId, MessageLink* source);
    void RetryPending(const std::string& failedNodeId);
    void ExpirePending(const std::string& requestId);
    void RetireLink(std::shared_ptr<MessageLink> link);
    std::shared_ptr<MessageLink> SelectLink(int64_t conversationId, const std::string& excluded = {});

    boost::asio::io_context& ioContext;
    boost::asio::thread_pool& controlPool;
    std::string gatewayId;
    std::string instanceId;
    std::string stateAddress;
    std::shared_ptr<grpc::ChannelCredentials> messageCredentials;
    std::atomic<bool> stopping{false};
    std::atomic<std::uint64_t> topologyVersion{0};

    mutable std::mutex linksMutex;
    std::unordered_map<std::string, Node> configuredNodes;
    std::unordered_map<std::string, std::shared_ptr<MessageLink>> links;
    std::vector<std::shared_ptr<MessageLink>> retiredLinks;
    std::unordered_map<std::string, unsigned int> reconnectAttempts;

    std::mutex pendingMutex;
    std::unordered_map<std::string, PendingCommand> pending;
    ClientForwardHandler clientForwardHandler;
};

} // namespace wimi::connection
