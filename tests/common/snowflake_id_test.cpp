#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <thread>
#include <vector>

#define private public
#include "server/common/snowflake_id.h"
#undef private

using namespace mir2::common;

class SnowflakeIdGeneratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        generator_ = std::make_unique<SnowflakeIdGenerator>(0);
    }

    std::unique_ptr<SnowflakeIdGenerator> generator_;
};

TEST_F(SnowflakeIdGeneratorTest, GenerateId) {
    uint64_t id = generator_->next_id();
    EXPECT_GT(id, 0u);
}

TEST_F(SnowflakeIdGeneratorTest, GenerateUniqueIds) {
    std::set<uint64_t> ids;
    for (int i = 0; i < 1000; ++i) {
        uint64_t id = generator_->next_id();
        EXPECT_TRUE(ids.insert(id).second);
    }
    EXPECT_EQ(ids.size(), 1000u);
}

TEST_F(SnowflakeIdGeneratorTest, IdsAreMonotonicallyIncreasing) {
    uint64_t prev_id = 0;
    for (int i = 0; i < 100; ++i) {
        uint64_t id = generator_->next_id();
        EXPECT_GT(id, prev_id);
        prev_id = id;
    }
}

TEST_F(SnowflakeIdGeneratorTest, ConcurrentGenerationAcrossWorkers) {
    const int num_threads = 4;
    const int ids_per_thread = 250;
    std::vector<std::vector<uint64_t>> thread_ids(num_threads);
    std::vector<std::thread> threads;

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, ids_per_thread, &thread_ids]() {
            SnowflakeIdGenerator generator(static_cast<uint16_t>(t));
            for (int i = 0; i < ids_per_thread; ++i) {
                thread_ids[t].push_back(generator.next_id());
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::set<uint64_t> all_ids;
    for (const auto& ids : thread_ids) {
        for (uint64_t id : ids) {
            EXPECT_TRUE(all_ids.insert(id).second);
        }
    }
    EXPECT_EQ(all_ids.size(), static_cast<size_t>(num_threads * ids_per_thread));
}

TEST_F(SnowflakeIdGeneratorTest, DifferentWorkerIds) {
    SnowflakeIdGenerator gen1(0);
    SnowflakeIdGenerator gen2(1);

    uint64_t id1 = gen1.next_id();
    uint64_t id2 = gen2.next_id();

    uint64_t worker1 = (id1 >> 12) & 0x3FF;
    uint64_t worker2 = (id2 >> 12) & 0x3FF;

    EXPECT_EQ(worker1, 0u);
    EXPECT_EQ(worker2, 1u);
}

TEST_F(SnowflakeIdGeneratorTest, InvalidWorkerIdThrows) {
    EXPECT_THROW(SnowflakeIdGenerator(1024), std::invalid_argument);
    EXPECT_THROW(SnowflakeIdGenerator(2048), std::invalid_argument);
}

TEST_F(SnowflakeIdGeneratorTest, ClockMovedBackwardsThrows) {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    generator_->last_timestamp_ = static_cast<uint64_t>(now_ms + 1000);
    EXPECT_THROW(generator_->next_id(), std::runtime_error);
}

TEST_F(SnowflakeIdGeneratorTest, PerformanceTest) {
    const int num_ids = 1000;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_ids; ++i) {
        generator_->next_id();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    EXPECT_LT(duration, 1000) << "Generated " << num_ids << " IDs in " << duration << "ms";
}
