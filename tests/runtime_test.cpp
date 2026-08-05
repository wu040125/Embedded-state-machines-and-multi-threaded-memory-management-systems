#include <edge_sentinel/runtime/edge_runtime.hpp>
#include <edge_sentinel/simulation/simulated_sensor.hpp>
#include <test_support.hpp>

#include <chrono>
#include <memory>
#include <thread>

namespace {

using namespace std::chrono_literals;
using edge_sentinel::domain::Event;
using edge_sentinel::domain::MotorState;
using edge_sentinel::domain::SensorSample;

bool wait_for_state(
    const edge_sentinel::runtime::EdgeRuntime& runtime,
    MotorState expected,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (runtime.snapshot().machine.state == expected) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

void test_runtime_lifecycle_and_fault_injection() {
    auto sensor = std::make_shared<edge_sentinel::simulation::SimulatedSensor>(
        SensorSample{45.0, 2.0, 6.0, true, 0});

    edge_sentinel::runtime::RuntimeConfig config;
    config.sample_period = 2ms;
    config.monitor_period = 5ms;
    config.event_push_timeout = 20ms;
    config.protection.warning_samples = 2;
    config.protection.critical_samples = 2;
    config.protection.recovery_samples = 2;
    config.protection.offline_samples = 2;

    edge_sentinel::runtime::EdgeRuntime runtime{sensor, config};
    ES_REQUIRE(runtime.start());
    ES_REQUIRE(!runtime.start());
    ES_REQUIRE(wait_for_state(runtime, MotorState::healthy, 500ms));

    sensor->set_override(SensorSample{105.0, 2.0, 6.0, true, 0});
    ES_REQUIRE(wait_for_state(runtime, MotorState::fault_latched, 500ms));

    sensor->clear_override();
    ES_REQUIRE_EQ(
        runtime.publish(Event::reset_requested()),
        edge_sentinel::runtime::PublishResult::published);
    ES_REQUIRE_EQ(
        runtime.publish(Event::self_test_passed()),
        edge_sentinel::runtime::PublishResult::published);
    ES_REQUIRE(wait_for_state(runtime, MotorState::healthy, 500ms));

    std::this_thread::sleep_for(10ms);
    const auto before_stop = runtime.snapshot();
    ES_REQUIRE(before_stop.metrics.events_published > 0);
    ES_REQUIRE(before_stop.metrics.events_processed > 0);
    ES_REQUIRE(before_stop.metrics.transitions >= std::uint64_t{5});
    ES_REQUIRE(before_stop.metrics.monitor_ticks > 0);

    runtime.stop();
    ES_REQUIRE(!runtime.running());
    const auto stopped = runtime.snapshot();
    ES_REQUIRE_EQ(stopped.machine.state, MotorState::safe_shutdown);
    ES_REQUIRE_EQ(stopped.pool.in_use, std::size_t{0});
    ES_REQUIRE_EQ(stopped.queue.size, std::size_t{0});

    runtime.stop();
}

}  // namespace

int main() {
    return edge_sentinel::test::run([] {
        test_runtime_lifecycle_and_fault_injection();
    });
}
