#pragma once

#include <cstdint>

namespace edge_sentinel::domain {

struct SensorSample {
    double temperature_c{0.0};
    double vibration_mm_s{0.0};
    double current_a{0.0};
    bool online{true};
    std::uint64_t sequence{0};
};

enum class EventKind {
    startup,
    self_test_passed,
    sensor_sample,
    reset_requested,
    shutdown_requested,
    internal_fault,
};

struct Event {
    EventKind kind{EventKind::startup};
    SensorSample sample{};

    [[nodiscard]] static constexpr Event startup() noexcept {
        return Event{EventKind::startup, {}};
    }

    [[nodiscard]] static constexpr Event self_test_passed() noexcept {
        return Event{EventKind::self_test_passed, {}};
    }

    [[nodiscard]] static constexpr Event sensor_sample(SensorSample value) noexcept {
        return Event{EventKind::sensor_sample, value};
    }

    [[nodiscard]] static constexpr Event reset_requested() noexcept {
        return Event{EventKind::reset_requested, {}};
    }

    [[nodiscard]] static constexpr Event shutdown_requested() noexcept {
        return Event{EventKind::shutdown_requested, {}};
    }

    [[nodiscard]] static constexpr Event internal_fault() noexcept {
        return Event{EventKind::internal_fault, {}};
    }
};

}  // namespace edge_sentinel::domain
