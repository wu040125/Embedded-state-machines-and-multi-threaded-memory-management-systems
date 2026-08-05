#include <edge_sentinel/memory/fixed_object_pool.hpp>
#include <test_support.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct LifetimeProbe {
    explicit LifetimeProbe(int initial_value) : value(initial_value) {
        ++constructions;
    }

    ~LifetimeProbe() {
        ++destructions;
    }

    LifetimeProbe(const LifetimeProbe&) = delete;
    LifetimeProbe& operator=(const LifetimeProbe&) = delete;

    int value;
    static inline int constructions{0};
    static inline int destructions{0};
};

struct alignas(64) CacheLineValue {
    std::uint64_t values[8]{};
};

void test_raii_and_statistics() {
    using edge_sentinel::memory::FixedObjectPool;

    LifetimeProbe::constructions = 0;
    LifetimeProbe::destructions = 0;

    FixedObjectPool<LifetimeProbe, 2> pool;
    ES_REQUIRE_EQ(pool.stats().capacity, std::size_t{2});

    auto first = pool.try_create(11);
    auto second = pool.try_create(22);
    ES_REQUIRE(first);
    ES_REQUIRE(second);
    ES_REQUIRE_EQ(first->value, 11);
    ES_REQUIRE_EQ(second->value, 22);
    ES_REQUIRE_EQ(pool.stats().in_use, std::size_t{2});
    ES_REQUIRE_EQ(pool.stats().high_watermark, std::size_t{2});

    auto exhausted = pool.try_create(33);
    ES_REQUIRE(!exhausted);
    ES_REQUIRE_EQ(pool.stats().allocation_failures, std::size_t{1});

    auto moved = std::move(first);
    ES_REQUIRE(!first);
    ES_REQUIRE(moved);
    moved.reset();
    ES_REQUIRE_EQ(pool.stats().in_use, std::size_t{1});

    second.reset();
    ES_REQUIRE_EQ(pool.outstanding(), std::size_t{0});
    ES_REQUIRE_EQ(LifetimeProbe::constructions, 2);
    ES_REQUIRE_EQ(LifetimeProbe::destructions, 2);
}

void test_alignment() {
    edge_sentinel::memory::FixedObjectPool<CacheLineValue, 1> pool;
    auto value = pool.try_create();
    ES_REQUIRE(value);

    const auto address = reinterpret_cast<std::uintptr_t>(value.get());
    ES_REQUIRE_EQ(address % alignof(CacheLineValue), std::uintptr_t{0});
}

void test_invalid_and_double_release_detection() {
    using edge_sentinel::memory::FixedObjectPool;
    using edge_sentinel::memory::PoolReleaseResult;

    FixedObjectPool<LifetimeProbe, 1> pool;
    auto value = pool.try_create(7);
    LifetimeProbe* raw = value.release();

    ES_REQUIRE_EQ(pool.destroy(raw), PoolReleaseResult::released);
    ES_REQUIRE_EQ(pool.destroy(raw), PoolReleaseResult::double_release);

    LifetimeProbe foreign{9};
    ES_REQUIRE_EQ(pool.destroy(&foreign), PoolReleaseResult::foreign_pointer);
    ES_REQUIRE_EQ(pool.stats().invalid_releases, std::size_t{2});
}

void test_concurrent_allocation_and_release() {
    constexpr int kThreadCount = 4;
    constexpr int kIterationsPerThread = 1'000;

    edge_sentinel::memory::FixedObjectPool<std::uint64_t, 8> pool;
    std::atomic<int> completed{0};
    std::vector<std::jthread> threads;
    threads.reserve(kThreadCount);

    for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
        threads.emplace_back([&pool, &completed, thread_index] {
            for (int iteration = 0; iteration < kIterationsPerThread; ++iteration) {
                decltype(pool)::Handle value;
                while (!value) {
                    value = pool.try_create(
                        static_cast<std::uint64_t>(thread_index * kIterationsPerThread + iteration));
                    if (!value) {
                        std::this_thread::yield();
                    }
                }
                ++completed;
            }
        });
    }
    threads.clear();

    ES_REQUIRE_EQ(completed.load(), kThreadCount * kIterationsPerThread);
    ES_REQUIRE_EQ(pool.outstanding(), std::size_t{0});
    ES_REQUIRE_EQ(
        pool.stats().successful_allocations,
        static_cast<std::size_t>(kThreadCount * kIterationsPerThread));
    ES_REQUIRE_EQ(pool.stats().successful_allocations, pool.stats().successful_releases);
}

}  // namespace

int main() {
    return edge_sentinel::test::run([] {
        test_raii_and_statistics();
        test_alignment();
        test_invalid_and_double_release_detection();
        test_concurrent_allocation_and_release();
    });
}
