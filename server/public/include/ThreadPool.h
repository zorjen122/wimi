#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "RequestContext.h"

namespace wimi
{

struct ThreadPoolOptions {
    std::size_t maxQueueSize{100000};
};

struct ThreadPoolStats {
    std::size_t workerCount{0};
    std::size_t queueSize{0};
    uint64_t submitted{0};
    uint64_t completed{0};
    uint64_t rejected{0};
    uint64_t acquireTimeouts{0};
    uint64_t expired{0};
};

class ThreadPool
{
  public:
    using Task = std::function<void()>;
    enum class PostStatus { Accepted, TimedOut, Stopped };

    ThreadPool(std::string name, std::size_t workerCount, ThreadPoolOptions options = {});
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool Post(Task task);
    // acquireDeadline 限制等待队列空位，taskDeadline 记录请求的最终截止时间。
    // 工作者发现任务过期后仍调用它，由业务层返回明确错误而不是静默丢任务。
    PostStatus PostUntil(Task task, RequestContext::Deadline acquireDeadline, RequestContext::Deadline taskDeadline);
    void Stop();
    ThreadPoolStats GetStats() const;

  private:
    void WorkerLoop(std::size_t index);
    struct TaskEntry {
        Task task;
        RequestContext::Deadline deadline{RequestContext::Deadline::max()};
    };

    bool PopTask(TaskEntry& task);

    std::string name;
    ThreadPoolOptions options;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::condition_variable queueSpace;
    std::queue<TaskEntry> queue;
    std::vector<std::thread> workers;
    bool stopping{false};

    std::atomic<uint64_t> submitted{0};
    std::atomic<uint64_t> completed{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<uint64_t> acquireTimeouts{0};
    std::atomic<uint64_t> expired{0};
};

} // namespace wimi
