#include <edge_sentinel/concurrency/bounded_queue.hpp>
#include <test_support.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;
using edge_sentinel::concurrency::QueuePushResult;

void test_fifo_capacity_and_statistics() {
    edge_sentinel::concurrency::BoundedQueue<std::unique_ptr<int>, 2> queue;

    ES_REQUIRE_EQ(queue.try_push(std::make_unique<int>(10)), QueuePushResult::pushed);
    ES_REQUIRE_EQ(queue.try_push(std::make_unique<int>(20)), QueuePushResult::pushed);
    ES_REQUIRE_EQ(queue.try_push(std::make_unique<int>(30)), QueuePushResult::full);

    auto first = queue.try_pop();
    auto second = queue.try_pop();
    ES_REQUIRE(first.has_value());
    ES_REQUIRE(second.has_value());
    ES_REQUIRE_EQ(**first, 10);
    ES_REQUIRE_EQ(**second, 20);
    ES_REQUIRE(!queue.try_pop().has_value());

    const auto stats = queue.stats();
    ES_REQUIRE_EQ(stats.high_watermark, std::size_t{2});
    ES_REQUIRE_EQ(stats.pushed, std::size_t{2});
    ES_REQUIRE_EQ(stats.popped, std::size_t{2});
    ES_REQUIRE_EQ(stats.rejected_full, std::size_t{1});
}

void test_close_wakes_waiting_consumer() {
    edge_sentinel::concurrency::BoundedQueue<int, 1> queue;
    std::promise<void> waiting;
    auto result = std::async(std::launch::async, [&queue, &waiting] {
        waiting.set_value();
        return queue.wait_pop();
    });

    waiting.get_future().wait();
    queue.close();
    ES_REQUIRE(!result.get().has_value());
    ES_REQUIRE(queue.closed());
    ES_REQUIRE_EQ(queue.try_push(1), QueuePushResult::closed);

    edge_sentinel::concurrency::BoundedQueue<int, 1> full_queue;
    ES_REQUIRE_EQ(full_queue.try_push(7), QueuePushResult::pushed);
    auto blocked_producer = std::async(std::launch::async, [&full_queue] {
        return full_queue.wait_push(8, 500ms);
    });
    std::this_thread::sleep_for(10ms);
    full_queue.close();
    ES_REQUIRE_EQ(blocked_producer.get(), QueuePushResult::closed);
    ES_REQUIRE_EQ(full_queue.wait_pop().value(), 7);
    ES_REQUIRE(!full_queue.wait_pop().has_value());
}

void test_waiting_producer_observes_available_space() {
    edge_sentinel::concurrency::BoundedQueue<int, 1> queue;
    ES_REQUIRE_EQ(queue.try_push(1), QueuePushResult::pushed);

    auto producer = std::async(std::launch::async, [&queue] {
        return queue.wait_push(2, 500ms);
    });

    std::this_thread::sleep_for(10ms);
    ES_REQUIRE_EQ(queue.try_pop().value(), 1);
    ES_REQUIRE_EQ(producer.get(), QueuePushResult::pushed);
    ES_REQUIRE_EQ(queue.try_pop().value(), 2);
}

void test_multiple_producers() {
    constexpr int kProducerCount = 4;
    constexpr int kValuesPerProducer = 500;
    constexpr int kExpected = kProducerCount * kValuesPerProducer;

    edge_sentinel::concurrency::BoundedQueue<int, 16> queue;
    std::atomic<int> produced{0};
    std::vector<std::jthread> producers;
    producers.reserve(kProducerCount);

    auto consumer = std::async(std::launch::async, [&queue] {
        int consumed = 0;
        while (consumed < kExpected) {
            if (queue.wait_pop().has_value()) {
                ++consumed;
            }
        }
        return consumed;
    });

    for (int producer_index = 0; producer_index < kProducerCount; ++producer_index) {
        producers.emplace_back([&queue, &produced, producer_index] {
            for (int value_index = 0; value_index < kValuesPerProducer; ++value_index) {
                const int value = producer_index * kValuesPerProducer + value_index;
                while (queue.wait_push(value, 100ms) != QueuePushResult::pushed) {
                }
                ++produced;
            }
        });
    }

    producers.clear();
    ES_REQUIRE_EQ(consumer.get(), kExpected);
    queue.close();
    ES_REQUIRE_EQ(produced.load(), kExpected);
    ES_REQUIRE_EQ(queue.stats().pushed, static_cast<std::size_t>(kExpected));
    ES_REQUIRE_EQ(queue.stats().popped, static_cast<std::size_t>(kExpected));
}

}  // namespace

int main() {
    return edge_sentinel::test::run([] {
        test_fifo_capacity_and_statistics();
        test_close_wakes_waiting_consumer();
        test_waiting_producer_observes_available_space();
        test_multiple_producers();
    });
}
