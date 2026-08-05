#include <edge_sentinel/linux/sensor_adapters.hpp>

#include <edge_sentinel/protocols/can_protocol.hpp>
#include <edge_sentinel/protocols/measurement.hpp>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace edge_sentinel::linux_platform {
namespace {

speed_t baud_constant(int baud_rate) noexcept {
    switch (baud_rate) {
    case 9'600:
        return B9600;
    case 57'600:
        return B57600;
    case 115'200:
        return B115200;
    default:
        return 0;
    }
}

bool wait_for_input(int descriptor, int timeout_ms) noexcept {
    pollfd item{descriptor, POLLIN, 0};
    return ::poll(&item, 1, timeout_ms) > 0 && (item.revents & POLLIN) != 0;
}

}  // namespace

UartSensorSource::UartSensorSource(const std::string& device_path, int baud_rate) {
    const speed_t speed = baud_constant(baud_rate);
    if (speed == 0) {
        error_number_ = EINVAL;
        return;
    }

    UniqueFd candidate{
        ::open(device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC)};
    if (!candidate) {
        error_number_ = errno;
        return;
    }

    termios settings{};
    if (::tcgetattr(candidate.get(), &settings) != 0) {
        error_number_ = errno;
        return;
    }
    ::cfmakeraw(&settings);
    static_cast<void>(::cfsetispeed(&settings, speed));
    static_cast<void>(::cfsetospeed(&settings, speed));
    settings.c_cflag = static_cast<tcflag_t>(
        settings.c_cflag | static_cast<tcflag_t>(CLOCAL | CREAD));
    settings.c_cflag = static_cast<tcflag_t>(
        settings.c_cflag & ~static_cast<tcflag_t>(CSTOPB));
    settings.c_cflag = static_cast<tcflag_t>(
        settings.c_cflag & ~static_cast<tcflag_t>(CRTSCTS));
    settings.c_cc[VMIN] = static_cast<cc_t>(0);
    settings.c_cc[VTIME] = static_cast<cc_t>(1);
    if (::tcsetattr(candidate.get(), TCSANOW, &settings) != 0) {
        error_number_ = errno;
        return;
    }
    descriptor_ = std::move(candidate);
    sample_.online = true;
}

domain::SensorSample UartSensorSource::read() {
    ++sequence_;
    bool received_measurement = false;
    if (descriptor_ && wait_for_input(descriptor_.get(), 50)) {
        std::array<std::byte, 128> bytes{};
        const ssize_t count = ::read(descriptor_.get(), bytes.data(), bytes.size());
        if (count > 0) {
            for (ssize_t index = 0; index < count; ++index) {
                const auto frame = decoder_.feed(bytes[static_cast<std::size_t>(index)]);
                if (frame) {
                    const protocols::Measurement measurement =
                        protocols::decode_uart_measurement(*frame);
                    protocols::apply_measurement(sample_, measurement);
                    if (measurement.kind != protocols::MeasurementKind::online) {
                        sample_.online = true;
                    }
                    received_measurement = true;
                }
            }
        } else if (count < 0 && errno != EAGAIN && errno != EINTR) {
            error_number_ = errno;
        }
    }
    if (!received_measurement) {
        sample_.online = false;
    }
    sample_.sequence = sequence_;
    return sample_;
}

bool UartSensorSource::valid() const noexcept {
    return static_cast<bool>(descriptor_);
}

int UartSensorSource::error_number() const noexcept {
    return error_number_;
}

SocketCanSensorSource::SocketCanSensorSource(const std::string& interface_name) {
    if (interface_name.empty() || interface_name.size() >= IFNAMSIZ) {
        error_number_ = EINVAL;
        return;
    }

    UniqueFd candidate{::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW)};
    if (!candidate) {
        error_number_ = errno;
        return;
    }

    ifreq request{};
    std::memcpy(request.ifr_name, interface_name.c_str(), interface_name.size() + 1);
    if (::ioctl(candidate.get(), SIOCGIFINDEX, &request) != 0) {
        error_number_ = errno;
        return;
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = request.ifr_ifindex;
    if (::bind(
            candidate.get(),
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) != 0) {
        error_number_ = errno;
        return;
    }
    descriptor_ = std::move(candidate);
    sample_.online = true;
}

domain::SensorSample SocketCanSensorSource::read() {
    ++sequence_;
    bool received_measurement = false;
    if (descriptor_ && wait_for_input(descriptor_.get(), 50)) {
        can_frame frame{};
        const ssize_t count = ::read(descriptor_.get(), &frame, sizeof(frame));
        if (count == static_cast<ssize_t>(sizeof(frame))) {
            protocols::CanMessage message;
            message.id = static_cast<std::uint32_t>(frame.can_id & CAN_EFF_MASK);
            message.length = static_cast<std::uint8_t>(frame.can_dlc);
            for (std::size_t index = 0; index < message.data.size(); ++index) {
                message.data[index] = frame.data[index];
            }
            if (const auto measurement = protocols::decode_can_message(message)) {
                protocols::apply_measurement(sample_, *measurement);
                if (measurement->kind != protocols::MeasurementKind::online) {
                    sample_.online = true;
                }
                received_measurement = true;
            }
        } else if (count < 0 && errno != EAGAIN && errno != EINTR) {
            error_number_ = errno;
        }
    }
    if (!received_measurement) {
        sample_.online = false;
    }
    sample_.sequence = sequence_;
    return sample_;
}

bool SocketCanSensorSource::valid() const noexcept {
    return static_cast<bool>(descriptor_);
}

int SocketCanSensorSource::error_number() const noexcept {
    return error_number_;
}

LinuxI2cBus::LinuxI2cBus(const std::string& device_path, std::uint16_t device_address)
    : device_address_(device_address) {
    if (device_address > 0x7FU) {
        error_number_ = EINVAL;
        return;
    }
    descriptor_.reset(::open(device_path.c_str(), O_RDWR | O_CLOEXEC));
    if (!descriptor_) {
        error_number_ = errno;
    }
}

bool LinuxI2cBus::read_register(std::uint8_t reg, std::span<std::byte> output) {
    if (!descriptor_ || output.empty() ||
        output.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        error_number_ = EINVAL;
        return false;
    }

    std::uint8_t register_value = reg;
    i2c_msg messages[2]{};
    messages[0].addr = static_cast<__u16>(device_address_);
    messages[0].flags = static_cast<__u16>(0);
    messages[0].len = static_cast<__u16>(1);
    messages[0].buf = &register_value;
    messages[1].addr = static_cast<__u16>(device_address_);
    messages[1].flags = static_cast<__u16>(I2C_M_RD);
    messages[1].len = static_cast<__u16>(output.size());
    messages[1].buf = reinterpret_cast<std::uint8_t*>(output.data());

    i2c_rdwr_ioctl_data transaction{};
    transaction.msgs = messages;
    transaction.nmsgs = static_cast<__u32>(2);
    if (::ioctl(descriptor_.get(), I2C_RDWR, &transaction) != 2) {
        error_number_ = errno;
        return false;
    }
    return true;
}

bool LinuxI2cBus::valid() const noexcept {
    return static_cast<bool>(descriptor_);
}

int LinuxI2cBus::error_number() const noexcept {
    return error_number_;
}

I2cSensorSource::I2cSensorSource(
    const std::string& device_path,
    std::uint16_t device_address)
    : bus_(device_path, device_address), reader_(bus_) {}

domain::SensorSample I2cSensorSource::read() {
    return reader_.read();
}

bool I2cSensorSource::valid() const noexcept {
    return bus_.valid();
}

int I2cSensorSource::error_number() const noexcept {
    return bus_.error_number();
}

}  // namespace edge_sentinel::linux_platform
