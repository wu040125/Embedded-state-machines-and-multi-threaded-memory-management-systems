#pragma once

#include <edge_sentinel/protocols/measurement.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace edge_sentinel::protocols {

inline constexpr std::uint32_t kCanTemperatureId = 0x101;
inline constexpr std::uint32_t kCanVibrationId = 0x102;
inline constexpr std::uint32_t kCanCurrentId = 0x103;
inline constexpr std::uint32_t kCanOnlineId = 0x104;

struct CanMessage {
    std::uint32_t id{0};
    std::uint8_t length{0};
    std::array<std::uint8_t, 8> data{};
};

[[nodiscard]] inline std::optional<Measurement> decode_can_message(
    const CanMessage& message) noexcept {
    if (message.id == kCanOnlineId) {
        if (message.length < 1) {
            return std::nullopt;
        }
        return Measurement{MeasurementKind::online, message.data[0] == 0 ? 0.0 : 1.0};
    }
    if (message.length < 4) {
        return std::nullopt;
    }

    MeasurementKind kind;
    if (message.id == kCanTemperatureId) {
        kind = MeasurementKind::temperature;
    } else if (message.id == kCanVibrationId) {
        kind = MeasurementKind::vibration;
    } else if (message.id == kCanCurrentId) {
        kind = MeasurementKind::current;
    } else {
        return std::nullopt;
    }

    const std::uint32_t raw = static_cast<std::uint32_t>(message.data[0]) << 24U |
                              static_cast<std::uint32_t>(message.data[1]) << 16U |
                              static_cast<std::uint32_t>(message.data[2]) << 8U |
                              static_cast<std::uint32_t>(message.data[3]);
    const std::int32_t milli_value = std::bit_cast<std::int32_t>(raw);
    return Measurement{kind, static_cast<double>(milli_value) / 1'000.0};
}

}  // namespace edge_sentinel::protocols
