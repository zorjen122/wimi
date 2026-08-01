#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wimi
{

enum class Metric : std::size_t {
    RequestsStarted,
    RequestsSucceeded,
    RequestsFailed,
    RequestsExpired,
    ThreadPoolAcquireTimeout,
    ThreadPoolTaskExpired,
    MysqlAcquireTimeout,
    RedisAcquireTimeout,
    RpcAcquireTimeout,
    RpcDeadlineExceeded,
    IdempotencyAccepted,
    IdempotencyReplayed,
    Count,
};

// 当前只提供进程内原子计数骨架；后续接 Prometheus/OpenTelemetry 时，
// 业务代码继续使用这些稳定的指标名，不直接依赖具体采集 SDK。
class Metrics
{
  public:
    static void Increment(Metric metric, uint64_t value = 1);
    static uint64_t Get(Metric metric);
    static std::string_view Name(Metric metric);

  private:
    using CounterArray = std::array<std::atomic<uint64_t>, static_cast<std::size_t>(Metric::Count)>;

    static CounterArray& Counters();
};

} // namespace wimi
