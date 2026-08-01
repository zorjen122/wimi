#pragma once

#include <cstdint>
#include <sw/redis++/redis++.h>
#include <sw/redis++/redis.h>

class IDGenerator
{
  public:
    using IdType = int64_t;

    IDGenerator(sw::redis::Redis& redis, uint16_t machine_id) : redis(redis), machineId(machine_id) {}

    IdType generate(const std::string& key)
    {
        // 1. 获取当前毫秒时间戳（41位）
        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() - epoch;

        // 2. 获取序列号（12位，每毫秒4096个）
        auto seq = redis.incr(key + ":" + std::to_string(ts));
        if (seq > 4095) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return generate(key); // 递归直到不冲突
        }

        // 3. 组合ID：时间戳(41) + 机器ID(10) + 序列号(12)
        return (ts << 22) | (machineId << 12) | seq;
    }

  private:
    sw::redis::Redis& redis;
    uint16_t machineId;                  // 集群中每个节点的唯一ID (0-1023)
    const int64_t epoch = 1672531200000; // 2023-01-01 00:00:00
};
