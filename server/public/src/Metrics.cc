#include "Metrics.h"

namespace wimi
{

Metrics::CounterArray& Metrics::Counters()
{
    static CounterArray counters{};
    return counters;
}

void Metrics::Increment(Metric metric, uint64_t value)
{
    Counters()[static_cast<std::size_t>(metric)].fetch_add(value, std::memory_order_relaxed);
}

uint64_t Metrics::Get(Metric metric)
{
    return Counters()[static_cast<std::size_t>(metric)].load(std::memory_order_relaxed);
}

std::string_view Metrics::Name(Metric metric)
{
    static constexpr std::array<std::string_view, static_cast<std::size_t>(Metric::Count)> names = {
        "requests_started",      "requests_succeeded",          "requests_failed",
        "requests_expired",      "thread_pool_acquire_timeout", "thread_pool_task_expired",
        "mysql_acquire_timeout", "redis_acquire_timeout",       "rpc_acquire_timeout",
        "rpc_deadline_exceeded", "idempotency_accepted",        "idempotency_replayed"};
    return names[static_cast<std::size_t>(metric)];
}

} // namespace wimi
