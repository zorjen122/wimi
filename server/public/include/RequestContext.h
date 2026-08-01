#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace wimi
{

enum class RequestSource { Tcp, Rpc, Internal };

// 单个逻辑请求的可信元数据。deadline 使用单调时钟，避免系统时间回拨
// 导致请求预算被意外延长；跨 RPC 时只在边界转换成 system_clock。
class RequestContext
{
  public:
    using Clock = std::chrono::steady_clock;
    using Deadline = Clock::time_point;

    RequestContext() = default;
    RequestContext(std::string requestId, std::string operation, RequestSource source, int64_t actor, Deadline deadline)
        : startedAt(Clock::now()), requestId(std::move(requestId)), operation(std::move(operation)), source(source),
          actor(actor), deadline(deadline)
    {
    }

    static RequestContext WithTimeout(std::string requestId, std::string operation, RequestSource source, int64_t actor,
                                      std::chrono::milliseconds timeout)
    {
        timeout = std::max(timeout, std::chrono::milliseconds(1));
        return RequestContext(std::move(requestId), std::move(operation), source, actor, Clock::now() + timeout);
    }

    static std::string NextRequestId()
    {
        static std::atomic<uint64_t> next{1};
        return std::to_string(next.fetch_add(1, std::memory_order_relaxed));
    }

    bool Expired() const { return deadline != Deadline::max() && Clock::now() >= deadline; }

    std::chrono::milliseconds Remaining() const
    {
        if (deadline == Deadline::max()) {
            return std::chrono::milliseconds::max();
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        return std::max(remaining, std::chrono::milliseconds(0));
    }

    std::chrono::system_clock::time_point SystemDeadline() const
    {
        auto remaining = Remaining();
        if (remaining == std::chrono::milliseconds::max()) {
            return std::chrono::system_clock::time_point::max();
        }
        return std::chrono::system_clock::now() + remaining;
    }

    Deadline startedAt{Clock::now()};
    std::string requestId;
    std::string operation;
    RequestSource source{RequestSource::Internal};
    int64_t actor{0};
    Deadline deadline{Deadline::max()};
};

class RequestContextScope
{
  public:
    // 通过线程局部作用域向现有 DAO/RPC 接口传播上下文，避免业务函数各自
    // 创建互不一致的超时；异步切换线程时必须显式复制并重新建立 Scope。
    explicit RequestContextScope(RequestContext& context) : previous(current) { current = &context; }

    ~RequestContextScope() { current = previous; }

    RequestContextScope(const RequestContextScope&) = delete;
    RequestContextScope& operator=(const RequestContextScope&) = delete;

    static RequestContext* Current() { return current; }

    static RequestContext::Deadline CurrentDeadline()
    {
        return current == nullptr ? RequestContext::Deadline::max() : current->deadline;
    }

    static RequestContext::Deadline CurrentDeadlineOr(std::chrono::milliseconds fallback)
    {
        // 非请求后台任务同样必须有等待上限，禁止退化为无限阻塞。
        auto deadline = CurrentDeadline();
        return deadline == RequestContext::Deadline::max() ? RequestContext::Clock::now() + fallback : deadline;
    }

  private:
    RequestContext* previous;
    inline static thread_local RequestContext* current = nullptr;
};

} // namespace wimi
