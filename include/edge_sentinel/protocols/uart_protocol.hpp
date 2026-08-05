#pragma once

#include <edge_sentinel/protocols/measurement.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace edge_sentinel::protocols {

inline constexpr std::byte kUartMagicFirst{0xAA};
inline constexpr std::byte kUartMagicSecond{0x55};
inline constexpr std::byte kUartProtocolVersion{0x01};
inline constexpr std::size_t kUartFrameSize = 12;

struct UartFrame {
    MeasurementKind kind{MeasurementKind::temperature};
    std::uint16_t sequence{0};
    std::int32_t milli_value{0};
};

[[nodiscard]] inline std::uint16_t crc16_ccitt(std::span<const std::byte> bytes) noexcept {
    std::uint16_t crc = 0xFFFFU;
    for (const std::byte byte : bytes) {
        crc ^= static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(byte)) << 8U;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000U) != 0U
                      ? static_cast<std::uint16_t>((crc << 1U) ^ 0x1021U)
                      : static_cast<std::uint16_t>(crc << 1U);
        }
    }
    return crc;
}

[[nodiscard]] inline std::array<std::byte, kUartFrameSize> encode_uart_frame(
    const UartFrame& frame) noexcept {
    std::array<std::byte, kUartFrameSize> bytes{};
    bytes[0] = kUartMagicFirst;
    bytes[1] = kUartMagicSecond;
    bytes[2] = kUartProtocolVersion;
    bytes[3] = static_cast<std::byte>(frame.kind);
    bytes[4] = static_cast<std::byte>((frame.sequence >> 8U) & 0xFFU);
    bytes[5] = static_cast<std::byte>(frame.sequence & 0xFFU);

    const std::uint32_t raw = std::bit_cast<std::uint32_t>(frame.milli_value);
    bytes[6] = static_cast<std::byte>((raw >> 24U) & 0xFFU);
    bytes[7] = static_cast<std::byte>((raw >> 16U) & 0xFFU);
    bytes[8] = static_cast<std::byte>((raw >> 8U) & 0xFFU);
    bytes[9] = static_cast<std::byte>(raw & 0xFFU);

    const std::uint16_t crc = crc16_ccitt(std::span<const std::byte>{bytes}.first(10));
    bytes[10] = static_cast<std::byte>((crc >> 8U) & 0xFFU);
    bytes[11] = static_cast<std::byte>(crc & 0xFFU);
    return bytes;
}

class UartFrameDecoder final {
public:
    [[nodiscard]] std::optional<UartFrame> feed(std::byte byte) noexcept {
        if (position_ == 0) {
            if (byte == kUartMagicFirst) {
                buffer_[position_++] = byte;
            }
            return std::nullopt;
        }
        if (position_ == 1) {
            if (byte == kUartMagicSecond) {
                buffer_[position_++] = byte;
            } else if (byte != kUartMagicFirst) {
                position_ = 0;
            }
            return std::nullopt;
        }

        buffer_[position_++] = byte;
        if (position_ < buffer_.size()) {
            return std::nullopt;
        }
        position_ = 0;

        const std::uint16_t expected_crc = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(buffer_[10])) << 8U |
            std::to_integer<std::uint8_t>(buffer_[11]));
        const std::uint16_t actual_crc =
            crc16_ccitt(std::span<const std::byte>{buffer_}.first(10));
        if (expected_crc != actual_crc) {
            ++crc_errors_;
            return std::nullopt;
        }
        if (buffer_[2] != kUartProtocolVersion) {
            ++format_errors_;
            return std::nullopt;
        }

        const auto kind_value = std::to_integer<std::uint8_t>(buffer_[3]);
        if (kind_value < static_cast<std::uint8_t>(MeasurementKind::temperature) ||
            kind_value > static_cast<std::uint8_t>(MeasurementKind::online)) {
            ++format_errors_;
            return std::nullopt;
        }

        const std::uint16_t sequence = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(buffer_[4])) << 8U |
            std::to_integer<std::uint8_t>(buffer_[5]));
        const std::uint32_t raw =
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(buffer_[6])) << 24U |
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(buffer_[7])) << 16U |
            static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(buffer_[8])) << 8U |
            std::to_integer<std::uint8_t>(buffer_[9]);
        return UartFrame{
            static_cast<MeasurementKind>(kind_value),
            sequence,
            std::bit_cast<std::int32_t>(raw)};
    }

    [[nodiscard]] std::size_t crc_errors() const noexcept {
        return crc_errors_;
    }

    [[nodiscard]] std::size_t format_errors() const noexcept {
        return format_errors_;
    }

private:
    std::array<std::byte, kUartFrameSize> buffer_{};
    std::size_t position_{0};
    std::size_t crc_errors_{0};
    std::size_t format_errors_{0};
};

[[nodiscard]] inline Measurement decode_uart_measurement(const UartFrame& frame) noexcept {
    if (frame.kind == MeasurementKind::online) {
        return Measurement{frame.kind, frame.milli_value == 0 ? 0.0 : 1.0};
    }
    return Measurement{frame.kind, static_cast<double>(frame.milli_value) / 1'000.0};
}

}  // namespace edge_sentinel::protocols
