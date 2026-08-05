#pragma once

#include <edge_sentinel/domain/event.hpp>

namespace edge_sentinel::protocols {

enum class MeasurementKind {
    temperature = 1,
    vibration = 2,
    current = 3,
    online = 4,
};

struct Measurement {
    MeasurementKind kind{MeasurementKind::temperature};
    double value{0.0};
};

inline void apply_measurement(domain::SensorSample& sample, const Measurement& measurement) {
    switch (measurement.kind) {
    case MeasurementKind::temperature:
        sample.temperature_c = measurement.value;
        break;
    case MeasurementKind::vibration:
        sample.vibration_mm_s = measurement.value;
        break;
    case MeasurementKind::current:
        sample.current_a = measurement.value;
        break;
    case MeasurementKind::online:
        sample.online = measurement.value != 0.0;
        break;
    }
}

}  // namespace edge_sentinel::protocols
