#include <edge_sentinel/domain/format.hpp>
#include <edge_sentinel/ipc/command.hpp>
#include <edge_sentinel/linux/unix_command_server.hpp>
#include <edge_sentinel/runtime/edge_runtime.hpp>
#include <edge_sentinel/simulation/simulated_sensor.hpp>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace std::chrono_literals;

std::atomic<bool> stop_requested{false};

void handle_signal(int) {
    stop_requested.store(true, std::memory_order_release);
}

std::string default_socket_path() {
    return "/tmp/edge-sentinel-" + std::to_string(::geteuid()) + ".sock";
}

std::string build_response(
    std::string_view input,
    edge_sentinel::runtime::EdgeRuntime& runtime,
    edge_sentinel::simulation::SimulatedSensor& sensor) {
    using edge_sentinel::ipc::CommandKind;
    using edge_sentinel::ipc::InjectionTarget;

    const auto parsed = edge_sentinel::ipc::parse_command(input);
    if (!parsed.ok()) {
        return "error=" + std::string{edge_sentinel::ipc::to_string(parsed.error)};
    }

    if (parsed.command.kind == CommandKind::status) {
        const auto snapshot = runtime.snapshot();
        std::ostringstream output;
        output << "state=" << edge_sentinel::domain::to_string(snapshot.machine.state)
               << " temperature_c=" << snapshot.machine.last_sample.temperature_c
               << " vibration_mm_s=" << snapshot.machine.last_sample.vibration_mm_s
               << " current_a=" << snapshot.machine.last_sample.current_a
               << " online=" << (snapshot.machine.last_sample.online ? 1 : 0);
        return output.str();
    }

    if (parsed.command.kind == CommandKind::metrics) {
        const auto snapshot = runtime.snapshot();
        std::ostringstream output;
        output << "events_published=" << snapshot.metrics.events_published
               << " events_processed=" << snapshot.metrics.events_processed
               << " events_dropped=" << snapshot.metrics.events_dropped
               << " transitions=" << snapshot.metrics.transitions
               << " max_latency_ns=" << snapshot.metrics.max_event_latency_ns
               << " pool=" << snapshot.pool.in_use << '/' << snapshot.pool.capacity
               << " pool_high=" << snapshot.pool.high_watermark
               << " queue=" << snapshot.queue.size << '/' << snapshot.queue.capacity
               << " queue_high=" << snapshot.queue.high_watermark;
        return output.str();
    }

    if (parsed.command.kind == CommandKind::inject) {
        edge_sentinel::domain::SensorSample injected{45.0, 2.0, 6.0, true, 0};
        switch (parsed.command.target) {
        case InjectionTarget::temperature:
            injected.temperature_c = parsed.command.value;
            break;
        case InjectionTarget::vibration:
            injected.vibration_mm_s = parsed.command.value;
            break;
        case InjectionTarget::current:
            injected.current_a = parsed.command.value;
            break;
        case InjectionTarget::offline:
            injected.online = false;
            break;
        case InjectionTarget::none:
            return "error=unknown_target";
        }
        sensor.set_override(injected);
        return "ok=injection_set";
    }

    if (parsed.command.kind == CommandKind::clear) {
        sensor.clear_override();
        return "ok=injection_cleared";
    }

    if (parsed.command.kind == CommandKind::reset) {
        sensor.clear_override();
        const auto reset = runtime.publish(edge_sentinel::domain::Event::reset_requested());
        const auto self_test = runtime.publish(edge_sentinel::domain::Event::self_test_passed());
        if (reset != edge_sentinel::runtime::PublishResult::published ||
            self_test != edge_sentinel::runtime::PublishResult::published) {
            return "error=event_delivery_failed";
        }
        return "ok=reset_requested";
    }

    if (parsed.command.kind == CommandKind::shutdown) {
        const auto result = runtime.publish(edge_sentinel::domain::Event::shutdown_requested());
        return result == edge_sentinel::runtime::PublishResult::published
                   ? "ok=shutdown_requested"
                   : "error=event_delivery_failed";
    }

    return "error=unknown_command";
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string socket_path = default_socket_path();
    for (int index = 1; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--socket" && index + 1 < argc) {
            socket_path = argv[++index];
        } else {
            std::cerr << "usage: edge_sentinel_daemon [--socket PATH]\n";
            return 2;
        }
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    auto sensor = std::make_shared<edge_sentinel::simulation::SimulatedSensor>(
        edge_sentinel::domain::SensorSample{45.0, 2.0, 6.0, true, 0});
    edge_sentinel::runtime::EdgeRuntime runtime{sensor};
    if (!runtime.start()) {
        std::cerr << "failed_to_start_runtime\n";
        return 1;
    }

    edge_sentinel::linux_platform::UnixCommandServer server{
        socket_path,
        [&runtime, &sensor](std::string_view command) {
            return build_response(command, runtime, *sensor);
        }};
    std::string server_error;
    if (!server.start(server_error)) {
        std::cerr << "failed_to_start_server error=" << server_error << '\n';
        runtime.stop();
        return 1;
    }

    std::cout << "EdgeSentinel daemon socket=" << server.socket_path() << '\n';
    while (!stop_requested.load(std::memory_order_acquire) &&
           runtime.snapshot().machine.state != edge_sentinel::domain::MotorState::safe_shutdown) {
        std::this_thread::sleep_for(100ms);
    }

    if (stop_requested.load(std::memory_order_acquire)) {
        static_cast<void>(runtime.publish(edge_sentinel::domain::Event::shutdown_requested()));
    }
    server.stop();
    runtime.stop();
    return 0;
}
