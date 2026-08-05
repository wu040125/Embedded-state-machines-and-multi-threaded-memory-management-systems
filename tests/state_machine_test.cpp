#include <edge_sentinel/fsm/motor_state_machine.hpp>
#include <test_support.hpp>

namespace {

using edge_sentinel::domain::Event;
using edge_sentinel::domain::MotorState;
using edge_sentinel::domain::SensorSample;
using edge_sentinel::domain::TransitionReason;
using edge_sentinel::fsm::MotorStateMachine;
using edge_sentinel::fsm::ProtectionConfig;

ProtectionConfig fast_config() {
    ProtectionConfig config;
    config.warning_samples = 2;
    config.critical_samples = 2;
    config.recovery_samples = 2;
    config.offline_samples = 2;
    return config;
}

SensorSample normal_sample() {
    return SensorSample{45.0, 2.0, 6.0, true, 1};
}

void start_healthy(MotorStateMachine& machine) {
    ES_REQUIRE_EQ(machine.state(), MotorState::booting);
    ES_REQUIRE_EQ(machine.handle(Event::startup()).current, MotorState::self_test);
    ES_REQUIRE_EQ(machine.handle(Event::self_test_passed()).current, MotorState::healthy);
}

void test_warning_hysteresis_and_recovery() {
    MotorStateMachine machine{fast_config()};
    start_healthy(machine);

    SensorSample warning = normal_sample();
    warning.temperature_c = 85.0;
    ES_REQUIRE(!machine.handle(Event::sensor_sample(warning)).changed);
    const auto degraded = machine.handle(Event::sensor_sample(warning));
    ES_REQUIRE(degraded.changed);
    ES_REQUIRE_EQ(degraded.current, MotorState::degraded);
    ES_REQUIRE_EQ(degraded.reason, TransitionReason::warning_persisted);

    SensorSample inside_hysteresis = normal_sample();
    inside_hysteresis.temperature_c = 78.0;
    ES_REQUIRE(!machine.handle(Event::sensor_sample(inside_hysteresis)).changed);
    ES_REQUIRE_EQ(machine.state(), MotorState::degraded);

    ES_REQUIRE(!machine.handle(Event::sensor_sample(normal_sample())).changed);
    const auto recovered = machine.handle(Event::sensor_sample(normal_sample()));
    ES_REQUIRE(recovered.changed);
    ES_REQUIRE_EQ(recovered.current, MotorState::healthy);
    ES_REQUIRE_EQ(recovered.reason, TransitionReason::readings_recovered);
}

void test_critical_fault_is_latched_until_reset() {
    MotorStateMachine machine{fast_config()};
    start_healthy(machine);

    SensorSample critical = normal_sample();
    critical.vibration_mm_s = 12.0;
    ES_REQUIRE(!machine.handle(Event::sensor_sample(critical)).changed);
    const auto fault = machine.handle(Event::sensor_sample(critical));
    ES_REQUIRE_EQ(fault.current, MotorState::fault_latched);
    ES_REQUIRE_EQ(fault.reason, TransitionReason::critical_reading);

    ES_REQUIRE(!machine.handle(Event::sensor_sample(normal_sample())).changed);
    ES_REQUIRE_EQ(machine.state(), MotorState::fault_latched);

    const auto reset = machine.handle(Event::reset_requested());
    ES_REQUIRE_EQ(reset.current, MotorState::self_test);
    ES_REQUIRE_EQ(reset.reason, TransitionReason::manual_reset);
}

void test_sensor_offline_and_internal_fault() {
    MotorStateMachine offline_machine{fast_config()};
    start_healthy(offline_machine);
    SensorSample offline = normal_sample();
    offline.online = false;
    ES_REQUIRE(!offline_machine.handle(Event::sensor_sample(offline)).changed);
    const auto offline_fault = offline_machine.handle(Event::sensor_sample(offline));
    ES_REQUIRE_EQ(offline_fault.current, MotorState::fault_latched);
    ES_REQUIRE_EQ(offline_fault.reason, TransitionReason::sensor_offline);

    MotorStateMachine internal_machine{fast_config()};
    start_healthy(internal_machine);
    const auto internal_fault = internal_machine.handle(Event::internal_fault());
    ES_REQUIRE_EQ(internal_fault.current, MotorState::fault_latched);
    ES_REQUIRE_EQ(internal_fault.reason, TransitionReason::internal_resource_failure);
}

void test_safe_shutdown_from_any_active_state() {
    MotorStateMachine machine{fast_config()};
    start_healthy(machine);
    const auto shutdown = machine.handle(Event::shutdown_requested());
    ES_REQUIRE_EQ(shutdown.current, MotorState::safe_shutdown);
    ES_REQUIRE_EQ(shutdown.reason, TransitionReason::shutdown_requested);
    ES_REQUIRE(!machine.handle(Event::startup()).changed);
}

}  // namespace

int main() {
    return edge_sentinel::test::run([] {
        test_warning_hysteresis_and_recovery();
        test_critical_fault_is_latched_until_reset();
        test_sensor_offline_and_internal_fault();
        test_safe_shutdown_from_any_active_state();
    });
}
