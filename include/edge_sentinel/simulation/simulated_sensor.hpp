#pragma once

#include <edge_sentinel/hal/sensor_source.hpp>

#include <cstdint>
#include <mutex>
#include <optional>

namespace edge_sentinel::simulation {

class SimulatedSensor final : public hal::ISensorSource {
public:
    explicit SimulatedSensor(domain::SensorSample baseline) : baseline_(baseline) {}

    [[nodiscard]] domain::SensorSample read() override {
        std::lock_guard lock(mutex_);
        domain::SensorSample result = override_.value_or(baseline_);
        result.sequence = ++sequence_;
        return result;
    }

    void set_baseline(domain::SensorSample baseline) {
        std::lock_guard lock(mutex_);
        baseline_ = baseline;
    }

    void set_override(domain::SensorSample sample) {
        std::lock_guard lock(mutex_);
        override_ = sample;
    }

    void clear_override() {
        std::lock_guard lock(mutex_);
        override_.reset();
    }

private:
    std::mutex mutex_;
    domain::SensorSample baseline_{};
    std::optional<domain::SensorSample> override_{};
    std::uint64_t sequence_{0};
};

}  // namespace edge_sentinel::simulation
