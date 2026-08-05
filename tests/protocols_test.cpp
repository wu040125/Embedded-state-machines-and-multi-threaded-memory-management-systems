#include <edge_sentinel/protocols/can_protocol.hpp>
#include <edge_sentinel/protocols/i2c_sensor.hpp>
#include <edge_sentinel/protocols/uart_protocol.hpp>
#include <test_support.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

using edge_sentinel::protocols::MeasurementKind;

void test_uart_round_trip_fragmentation_and_crc() {
    const edge_sentinel::protocols::UartFrame input{
        MeasurementKind::temperature, 42, 105'500};
    const auto encoded = edge_sentinel::protocols::encode_uart_frame(input);

    edge_sentinel::protocols::UartFrameDecoder decoder;
    for (std::size_t index = 0; index + 1 < encoded.size(); ++index) {
        ES_REQUIRE(!decoder.feed(encoded[index]).has_value());
    }
    const auto decoded = decoder.feed(encoded.back());
    ES_REQUIRE(decoded.has_value());
    ES_REQUIRE_EQ(decoded->kind, MeasurementKind::temperature);
    ES_REQUIRE_EQ(decoded->sequence, std::uint16_t{42});
    ES_REQUIRE_EQ(decoded->milli_value, std::int32_t{105'500});

    auto corrupted = encoded;
    corrupted[6] ^= std::byte{0x01};
    edge_sentinel::protocols::UartFrameDecoder corrupt_decoder;
    for (const std::byte value : corrupted) {
        ES_REQUIRE(!corrupt_decoder.feed(value).has_value());
    }
    ES_REQUIRE_EQ(corrupt_decoder.crc_errors(), std::size_t{1});
}

void test_can_id_and_big_endian_mapping() {
    edge_sentinel::protocols::CanMessage message;
    message.id = edge_sentinel::protocols::kCanTemperatureId;
    message.length = 4;
    message.data = {0x00, 0x01, 0x9C, 0x1C, 0, 0, 0, 0};

    const auto measurement = edge_sentinel::protocols::decode_can_message(message);
    ES_REQUIRE(measurement.has_value());
    ES_REQUIRE_EQ(measurement->kind, MeasurementKind::temperature);
    ES_REQUIRE_EQ(measurement->value, 105.5);

    message.id = 0x777;
    ES_REQUIRE(!edge_sentinel::protocols::decode_can_message(message).has_value());
}

class FakeI2cBus final : public edge_sentinel::protocols::II2cBus {
public:
    bool read_register(std::uint8_t reg, std::span<std::byte> output) override {
        if (fail_reads) {
            return false;
        }
        const auto copy = [output](std::uint16_t value) {
            output[0] = static_cast<std::byte>((value >> 8U) & 0xFFU);
            output[1] = static_cast<std::byte>(value & 0xFFU);
        };
        if (reg == edge_sentinel::protocols::kTemperatureRegister) {
            copy(8'625);
        } else if (reg == edge_sentinel::protocols::kVibrationRegister) {
            copy(725);
        } else if (reg == edge_sentinel::protocols::kCurrentRegister) {
            copy(1'450);
        } else if (reg == edge_sentinel::protocols::kStatusRegister) {
            output[0] = std::byte{0x01};
        } else {
            return false;
        }
        return true;
    }

    bool fail_reads{false};
};

void test_i2c_register_map_and_read_failure() {
    FakeI2cBus bus;
    edge_sentinel::protocols::I2cSensorReader reader{bus};

    const auto sample = reader.read();
    ES_REQUIRE_EQ(sample.temperature_c, 86.25);
    ES_REQUIRE_EQ(sample.vibration_mm_s, 7.25);
    ES_REQUIRE_EQ(sample.current_a, 14.5);
    ES_REQUIRE(sample.online);

    bus.fail_reads = true;
    const auto failed = reader.read();
    ES_REQUIRE(!failed.online);
    ES_REQUIRE_EQ(failed.sequence, sample.sequence + 1);
}

}  // namespace

int main() {
    return edge_sentinel::test::run([] {
        test_uart_round_trip_fragmentation_and_crc();
        test_can_id_and_big_endian_mapping();
        test_i2c_register_map_and_read_failure();
    });
}
