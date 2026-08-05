#pragma once

#include <edge_sentinel/hal/sensor_source.hpp>
#include <edge_sentinel/linux/unique_fd.hpp>
#include <edge_sentinel/protocols/i2c_sensor.hpp>
#include <edge_sentinel/protocols/uart_protocol.hpp>

#include <cstdint>
#include <span>
#include <string>

namespace edge_sentinel::linux_platform {

/// Linux termios adapter for the EdgeSentinel CRC16 UART stream.
class UartSensorSource final : public hal::ISensorSource {
public:
    explicit UartSensorSource(const std::string& device_path, int baud_rate = 115'200);

    [[nodiscard]] domain::SensorSample read() override;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int error_number() const noexcept;

private:
    UniqueFd descriptor_{};
    protocols::UartFrameDecoder decoder_{};
    domain::SensorSample sample_{};
    std::uint64_t sequence_{0};
    int error_number_{0};
};

/// Linux SocketCAN adapter for the documented EdgeSentinel CAN IDs.
class SocketCanSensorSource final : public hal::ISensorSource {
public:
    explicit SocketCanSensorSource(const std::string& interface_name);

    [[nodiscard]] domain::SensorSample read() override;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int error_number() const noexcept;

private:
    UniqueFd descriptor_{};
    domain::SensorSample sample_{};
    std::uint64_t sequence_{0};
    int error_number_{0};
};

/// Linux i2c-dev implementation using a combined I2C_RDWR transaction.
class LinuxI2cBus final : public protocols::II2cBus {
public:
    LinuxI2cBus(const std::string& device_path, std::uint16_t device_address);

    [[nodiscard]] bool read_register(
        std::uint8_t reg,
        std::span<std::byte> output) override;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int error_number() const noexcept;

private:
    UniqueFd descriptor_{};
    std::uint16_t device_address_{0};
    int error_number_{0};
};

class I2cSensorSource final : public hal::ISensorSource {
public:
    I2cSensorSource(const std::string& device_path, std::uint16_t device_address);

    [[nodiscard]] domain::SensorSample read() override;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] int error_number() const noexcept;

private:
    LinuxI2cBus bus_;
    protocols::I2cSensorReader reader_;
};

}  // namespace edge_sentinel::linux_platform
