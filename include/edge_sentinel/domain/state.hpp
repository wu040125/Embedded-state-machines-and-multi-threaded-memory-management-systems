#pragma once

namespace edge_sentinel::domain {

enum class MotorState {
    booting,
    self_test,
    healthy,
    degraded,
    fault_latched,
    safe_shutdown,
};

enum class TransitionReason {
    none,
    startup_requested,
    self_test_succeeded,
    warning_persisted,
    readings_recovered,
    critical_reading,
    sensor_offline,
    internal_resource_failure,
    manual_reset,
    shutdown_requested,
};

struct StateTransition {
    MotorState previous{MotorState::booting};
    MotorState current{MotorState::booting};
    TransitionReason reason{TransitionReason::none};
    bool changed{false};
};

}  // namespace edge_sentinel::domain
