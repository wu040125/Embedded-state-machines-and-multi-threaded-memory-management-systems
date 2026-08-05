#pragma once

#include <edge_sentinel/domain/event.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace edge_sentinel::protocols {

inline constexpr std::uint8_t kTemperatureRegister = 0x01;
inline constexpr std::uint8_t kVibrationRegister = 0x03;
inline constexpr std::uint8_t kCurrentRegister = 0x05;
inline constexpr std::uint8_t kStatusRegister = 0x07;

class II2cBus {
public:
    virtual ~II2cBus() = default;

    [[nodiscard]] virtual bool read_register(
        std::uint8_t reg,
        std::span<std::byte> output) = 0;
};

class I2cSensorReader final {
public:
    explicit I2cSensorReader(II2cBus& bus) : bus_(bus) {}

    [[nodiscard]] domain::SensorSample read() {
        ++sequence_;
        std::array<std::byte, 2> temperature{};
        std::array<std::byte, 2> vibration{};
        std::array<std::byte, 2> current{};
        std::array<std::byte, 1> status{};

        if (!bus_.read_register(kTemperatureRegister, temperature) ||
            !bus_.read_register(kVibrationRegister, vibration) ||
            !bus_.read_register(kCurrentRegister, current) ||
            !bus_.read_register(kStatusRegister, status)) {
            last_sample_.online = false;
            last_sample_.sequence = sequence_;
            return last_sample_;
        }

        const std::uint16_t temperature_raw = decode_u16(temperature);
        const std::int16_t temperature_signed = std::bit_cast<std::int16_t>(temperature_raw);
        last_sample_ = domain::SensorSample{
            static_cast<double>(temperature_signed) / 100.0,
            static_cast<double>(decode_u16(vibration)) / 100.0,
            static_cast<double>(decode_u16(current)) / 100.0,
            (std::to_integer<std::uint8_t>(status[0]) & 0x01U) != 0U,
            sequence_};
        return last_sample_;
    }

private:
    [[nodiscard]] static std::uint16_t decode_u16(
        const std::array<std::byte, 2>& bytes) noexcept {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[0])) << 8U |
            std::to_integer<std::uint8_t>(bytes[1]));
    }

    II2cBus& bus_;
    domain::SensorSample last_sample_{};
    std::uint64_t sequence_{0};
};

}  // namespace edge_sentinel::protocols
