#pragma once
#include "spdlog/spdlog.h"
#include <atomic>
#include <condition_variable>
#include <grpcpp/channel.h>
#include <grpcpp/grpcpp.h>
#include <queue>

#include "Metrics.h"
#include "RequestContext.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

template <class RPC> class RpcPool
{
  public:
    RpcPool() = delete;

    RpcPool(size_t _poolSize, std::string _host, std::string _port) : isStop(false), poolSize(_poolSize)
    {
        for (size_t i = 0; i < _poolSize; ++i) {
            std::shared_ptr<Channel> channel =
                grpc::CreateChannel(_host + ":" + _port, grpc::InsecureChannelCredentials());
            bool isConnected = channel->WaitForConnected(std::chrono::system_clock::now() +
                                                         std::chrono::seconds(2)); // 2 seconds timeout
            if (!isConnected) {
                spdlog::warn("rpc connection failed, host: {}, port: {}, pool size: {}", _host, _port, _poolSize);
                --poolSize;
                continue;
            }
            channelQueue.push(RPC::NewStub(channel));
        }
    }

    ~RpcPool()
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        Close();
        while (!channelQueue.empty()) {
            channelQueue.pop();
        }
    }

    std::unique_ptr<typename RPC::Stub> getConnection()
    {
        return getConnectionUntil(wimi::RequestContextScope::CurrentDeadlineOr(std::chrono::milliseconds(1000)));
    }

    std::unique_ptr<typename RPC::Stub> getConnectionUntil(wimi::RequestContext::Deadline deadline)
    {
        // 这里只约束 stub 池等待；实际 RPC 还必须把同一 deadline 写入
        // ClientContext。
        std::unique_lock<std::mutex> lock(queueMutex);
        bool ready = cond.wait_until(lock, deadline, [this] {
            if (isStop) {
                return true;
            }
            return !channelQueue.empty();
        });
        if (!ready) {
            wimi::Metrics::Increment(wimi::Metric::RpcAcquireTimeout);
            spdlog::warn("RPC连接池获取超时, available: {}, capacity: {}", channelQueue.size(), poolSize);
            return nullptr;
        }
        if (isStop || channelQueue.empty()) {
            return nullptr;
        }
        auto con = std::move(channelQueue.front());
        channelQueue.pop();
        return con;
    }

    void returnConnection(std::unique_ptr<typename RPC::Stub> context)
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (isStop) {
            return;
        }
        channelQueue.push(std::move(context));
        cond.notify_one();
    }

    void Close()
    {
        isStop = true;
        cond.notify_all();
    }

    bool empty() { return channelQueue.empty(); }

    size_t getPoolSize() const { return poolSize; }

  private:
    std::atomic<bool> isStop;
    size_t poolSize;
    std::mutex queueMutex;
    std::condition_variable cond;
    std::queue<std::unique_ptr<typename RPC::Stub>> channelQueue;
};
