#pragma once

#include <edge_sentinel/domain/event.hpp>
#include <edge_sentinel/domain/state.hpp>

#include <cstddef>

namespace edge_sentinel::fsm {

struct ProtectionConfig {
    double temperature_warning_c{80.0};
    double temperature_recovery_c{75.0};
    double temperature_critical_c{95.0};
    double vibration_warning_mm_s{7.0};
    double vibration_recovery_mm_s{5.0};
    double vibration_critical_mm_s{10.0};
    double current_warning_a{15.0};
    double current_recovery_a{13.0};
    double current_critical_a{20.0};
    std::size_t warning_samples{3};
    std::size_t critical_samples{2};
    std::size_t recovery_samples{5};
    std::size_t offline_samples{3};
};

struct MachineSnapshot {
    domain::MotorState state{domain::MotorState::booting};
    domain::SensorSample last_sample{};
    bool has_sample{false};
    std::size_t warning_count{0};
    std::size_t critical_count{0};
    std::size_t recovery_count{0};
    std::size_t offline_count{0};
};

class MotorStateMachine final {
public:
    explicit MotorStateMachine(ProtectionConfig config = {}) : config_(config) {}

    [[nodiscard]] domain::MotorState state() const noexcept {
        return state_;
    }

    [[nodiscard]] MachineSnapshot snapshot() const noexcept {
        return MachineSnapshot{
            state_,
            last_sample_,
            has_sample_,
            warning_count_,
            critical_count_,
            recovery_count_,
            offline_count_};
    }

    [[nodiscard]] domain::StateTransition handle(const domain::Event& event) noexcept {
        using domain::EventKind;
        using domain::MotorState;
        using domain::TransitionReason;

        if (state_ == MotorState::safe_shutdown) {
            return unchanged();
        }

        if (event.kind == EventKind::shutdown_requested) {
            return transition_to(MotorState::safe_shutdown, TransitionReason::shutdown_requested);
        }
        if (event.kind == EventKind::internal_fault) {
            return transition_to(
                MotorState::fault_latched,
                TransitionReason::internal_resource_failure);
        }

        switch (event.kind) {
        case EventKind::startup:
            if (state_ == MotorState::booting) {
                return transition_to(MotorState::self_test, TransitionReason::startup_requested);
            }
            break;
        case EventKind::self_test_passed:
            if (state_ == MotorState::self_test) {
                return transition_to(MotorState::healthy, TransitionReason::self_test_succeeded);
            }
            break;
        case EventKind::sensor_sample:
            return handle_sample(event.sample);
        case EventKind::reset_requested:
            if (state_ == MotorState::fault_latched) {
                return transition_to(MotorState::self_test, TransitionReason::manual_reset);
            }
            break;
        case EventKind::shutdown_requested:
        case EventKind::internal_fault:
            break;
        }
        return unchanged();
    }

private:
    [[nodiscard]] domain::StateTransition handle_sample(
        const domain::SensorSample& sample) noexcept {
        using domain::MotorState;
        using domain::TransitionReason;

        last_sample_ = sample;
        has_sample_ = true;

        if (state_ != MotorState::healthy && state_ != MotorState::degraded) {
            return unchanged();
        }

        if (!sample.online) {
            ++offline_count_;
            warning_count_ = 0;
            critical_count_ = 0;
            recovery_count_ = 0;
            if (offline_count_ >= config_.offline_samples) {
                return transition_to(MotorState::fault_latched, TransitionReason::sensor_offline);
            }
            return unchanged();
        }
        offline_count_ = 0;

        if (is_critical(sample)) {
            ++critical_count_;
        } else {
            critical_count_ = 0;
        }
        if (critical_count_ >= config_.critical_samples) {
            return transition_to(MotorState::fault_latched, TransitionReason::critical_reading);
        }

        if (is_warning(sample)) {
            ++warning_count_;
            recovery_count_ = 0;
        } else {
            warning_count_ = 0;
            if (state_ == MotorState::degraded && is_recovered(sample)) {
                ++recovery_count_;
            } else {
                recovery_count_ = 0;
            }
        }

        if (state_ == MotorState::healthy && warning_count_ >= config_.warning_samples) {
            return transition_to(MotorState::degraded, TransitionReason::warning_persisted);
        }
        if (state_ == MotorState::degraded && recovery_count_ >= config_.recovery_samples) {
            return transition_to(MotorState::healthy, TransitionReason::readings_recovered);
        }
        return unchanged();
    }

    [[nodiscard]] bool is_warning(const domain::SensorSample& sample) const noexcept {
        return sample.temperature_c >= config_.temperature_warning_c ||
               sample.vibration_mm_s >= config_.vibration_warning_mm_s ||
               sample.current_a >= config_.current_warning_a;
    }

    [[nodiscard]] bool is_critical(const domain::SensorSample& sample) const noexcept {
        return sample.temperature_c >= config_.temperature_critical_c ||
               sample.vibration_mm_s >= config_.vibration_critical_mm_s ||
               sample.current_a >= config_.current_critical_a;
    }

    [[nodiscard]] bool is_recovered(const domain::SensorSample& sample) const noexcept {
        return sample.temperature_c <= config_.temperature_recovery_c &&
               sample.vibration_mm_s <= config_.vibration_recovery_mm_s &&
               sample.current_a <= config_.current_recovery_a;
    }

    [[nodiscard]] domain::StateTransition transition_to(
        domain::MotorState next,
        domain::TransitionReason reason) noexcept {
        const domain::MotorState previous = state_;
        state_ = next;
        reset_counters();
        return domain::StateTransition{previous, state_, reason, previous != state_};
    }

    [[nodiscard]] domain::StateTransition unchanged() const noexcept {
        return domain::StateTransition{
            state_, state_, domain::TransitionReason::none, false};
    }

    void reset_counters() noexcept {
        warning_count_ = 0;
        critical_count_ = 0;
        recovery_count_ = 0;
        offline_count_ = 0;
    }

    ProtectionConfig config_;
    domain::MotorState state_{domain::MotorState::booting};
    domain::SensorSample last_sample_{};
    bool has_sample_{false};
    std::size_t warning_count_{0};
    std::size_t critical_count_{0};
    std::size_t recovery_count_{0};
    std::size_t offline_count_{0};
};

}  // namespace edge_sentinel::fsm
