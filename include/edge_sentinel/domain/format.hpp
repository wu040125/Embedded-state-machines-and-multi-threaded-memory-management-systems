#pragma once

#include <edge_sentinel/domain/state.hpp>

#include <string_view>

namespace edge_sentinel::domain {

[[nodiscard]] constexpr std::string_view to_string(MotorState state) noexcept {
    switch (state) {
    case MotorState::booting:
        return "Booting";
    case MotorState::self_test:
        return "SelfTest";
    case MotorState::healthy:
        return "Healthy";
    case MotorState::degraded:
        return "Degraded";
    case MotorState::fault_latched:
        return "FaultLatched";
    case MotorState::safe_shutdown:
        return "SafeShutdown";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view to_string(TransitionReason reason) noexcept {
    switch (reason) {
    case TransitionReason::none:
        return "none";
    case TransitionReason::startup_requested:
        return "startup_requested";
    case TransitionReason::self_test_succeeded:
        return "self_test_succeeded";
    case TransitionReason::warning_persisted:
        return "warning_persisted";
    case TransitionReason::readings_recovered:
        return "readings_recovered";
    case TransitionReason::critical_reading:
        return "critical_reading";
    case TransitionReason::sensor_offline:
        return "sensor_offline";
    case TransitionReason::internal_resource_failure:
        return "internal_resource_failure";
    case TransitionReason::manual_reset:
        return "manual_reset";
    case TransitionReason::shutdown_requested:
        return "shutdown_requested";
    }
    return "unknown";
}

}  // namespace edge_sentinel::domain
