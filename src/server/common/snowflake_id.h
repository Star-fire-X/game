#ifndef MIR2_COMMON_SNOWFLAKE_ID_H
#define MIR2_COMMON_SNOWFLAKE_ID_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace mir2::common {

/**
 * @brief 雪花ID生成器（Snowflake ID）
 * 
 * 64位结构：
 * - 1位符号位（始终为0）
 * - 41位时间戳（毫秒，可用69年）
 * - 10位机器ID（支持1024台机器）
 * - 12位序列号（每毫秒4096个ID）
 */
class SnowflakeIdGenerator {
public:
    /**
     * @brief 构造函数
     * @param worker_id 工作机器ID (0-1023)
     * @param epoch 起始时间戳（默认2024-01-01 00:00:00 UTC）
     */
    explicit SnowflakeIdGenerator(uint16_t worker_id = 0,
                                   uint64_t epoch = 1704067200000ULL)
        : worker_id_(worker_id & 0x3FF)
        , epoch_(epoch)
        , sequence_(0)
        , last_timestamp_(0) {
        if (worker_id > 1023) {
            throw std::invalid_argument("Worker ID must be 0-1023");
        }
    }

    /**
     * @brief 生成下一个唯一ID
     * @return 64位唯一ID
     */
    uint64_t next_id() {
        uint64_t timestamp = current_timestamp();

        if (timestamp < last_timestamp_) {
            throw std::runtime_error("Clock moved backwards");
        }

        if (timestamp == last_timestamp_) {
            sequence_ = (sequence_ + 1) & 0xFFF;
            if (sequence_ == 0) {
                // 序列号溢出，等待下一毫秒
                while (timestamp <= last_timestamp_) {
                    timestamp = current_timestamp();
                }
            }
        } else {
            sequence_ = 0;
        }

        last_timestamp_ = timestamp;

        return ((timestamp - epoch_) << 22)
             | (worker_id_ << 12)
             | sequence_;
    }

private:
    uint64_t current_timestamp() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    uint16_t worker_id_;
    uint64_t epoch_;
    uint64_t sequence_;
    uint64_t last_timestamp_;
};

} // namespace mir2::common

#endif
