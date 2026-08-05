#include <edge_sentinel/domain/format.hpp>
#include <edge_sentinel/runtime/edge_runtime.hpp>
#include <edge_sentinel/simulation/simulated_sensor.hpp>
#include <edge_sentinel/version.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;

bool parse_positive_int(std::string_view text, int& value) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end && value > 0;
}

int read_option(int argc, char* argv[], std::string_view option, int fallback) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view{argv[index]} != option) {
            continue;
        }
        int parsed = 0;
        if (parse_positive_int(argv[index + 1], parsed)) {
            return parsed;
        }
    }
    return fallback;
}

}  // namespace

int main(int argc, char* argv[]) {
    const int duration_ms = read_option(argc, argv, "--duration-ms", 3'000);
    const int fault_after_ms = read_option(argc, argv, "--fault-after-ms", 1'000);

    auto sensor = std::make_shared<edge_sentinel::simulation::SimulatedSensor>(
        edge_sentinel::domain::SensorSample{45.0, 2.0, 6.0, true, 0});

    edge_sentinel::runtime::RuntimeConfig config;
    config.sample_period = 50ms;
    config.monitor_period = 250ms;
    edge_sentinel::runtime::EdgeRuntime runtime{sensor, config};

    std::cout << edge_sentinel::kProjectName << ' ' << edge_sentinel::kVersion
              << " simulated motor monitor\n";
    std::cout << "Injecting critical temperature after " << fault_after_ms << " ms\n";

    if (!runtime.start()) {
        std::cerr << "Failed to start runtime\n";
        return 1;
    }

    const auto started_at = std::chrono::steady_clock::now();
    auto last_state = edge_sentinel::domain::MotorState::booting;
    bool injected = false;

    while (std::chrono::steady_clock::now() - started_at <
           std::chrono::milliseconds{duration_ms}) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at);
        if (!injected && elapsed >= std::chrono::milliseconds{fault_after_ms}) {
            sensor->set_override(
                edge_sentinel::domain::SensorSample{105.0, 2.0, 6.0, true, 0});
            injected = true;
            std::cout << "[SIM] injected temperature=105 C\n";
        }

        const auto snapshot = runtime.snapshot();
        if (snapshot.machine.state != last_state) {
            std::cout << "[STATE] " << edge_sentinel::domain::to_string(last_state) << " -> "
                      << edge_sentinel::domain::to_string(snapshot.machine.state) << '\n';
            last_state = snapshot.machine.state;
        }
        std::this_thread::sleep_for(50ms);
    }

    runtime.stop();
    const auto final = runtime.snapshot();
    std::cout << "[FINAL] state=" << edge_sentinel::domain::to_string(final.machine.state)
              << " events=" << final.metrics.events_processed
              << " dropped=" << final.metrics.events_dropped
              << " pool_high_watermark=" << final.pool.high_watermark << '/'
              << final.pool.capacity << " queue_high_watermark=" << final.queue.high_watermark
              << '/' << final.queue.capacity << '\n';
    return 0;
}
