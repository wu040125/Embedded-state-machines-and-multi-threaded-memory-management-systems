#include <edge_sentinel/ipc/command.hpp>
#include <test_support.hpp>

#include <string>

namespace {

using edge_sentinel::ipc::CommandKind;
using edge_sentinel::ipc::InjectionTarget;
using edge_sentinel::ipc::ParseError;

void test_simple_commands() {
    ES_REQUIRE_EQ(edge_sentinel::ipc::parse_command("status").command.kind, CommandKind::status);
    ES_REQUIRE_EQ(
        edge_sentinel::ipc::parse_command(" metrics\n").command.kind,
        CommandKind::metrics);
    ES_REQUIRE_EQ(edge_sentinel::ipc::parse_command("reset").command.kind, CommandKind::reset);
    ES_REQUIRE_EQ(edge_sentinel::ipc::parse_command("clear").command.kind, CommandKind::clear);
    ES_REQUIRE_EQ(
        edge_sentinel::ipc::parse_command("shutdown").command.kind,
        CommandKind::shutdown);
}

void test_injection_commands() {
    const auto temperature = edge_sentinel::ipc::parse_command("inject temperature 105.5");
    ES_REQUIRE(temperature.ok());
    ES_REQUIRE_EQ(temperature.command.kind, CommandKind::inject);
    ES_REQUIRE_EQ(temperature.command.target, InjectionTarget::temperature);
    ES_REQUIRE_EQ(temperature.command.value, 105.5);

    const auto vibration = edge_sentinel::ipc::parse_command("inject vibration 12");
    ES_REQUIRE_EQ(vibration.command.target, InjectionTarget::vibration);

    const auto current = edge_sentinel::ipc::parse_command("inject current 24");
    ES_REQUIRE_EQ(current.command.target, InjectionTarget::current);

    const auto offline = edge_sentinel::ipc::parse_command("inject offline");
    ES_REQUIRE(offline.ok());
    ES_REQUIRE_EQ(offline.command.target, InjectionTarget::offline);
}

void test_rejects_untrusted_input() {
    ES_REQUIRE_EQ(edge_sentinel::ipc::parse_command("").error, ParseError::empty);
    ES_REQUIRE_EQ(edge_sentinel::ipc::parse_command("unknown").error, ParseError::unknown_command);
    ES_REQUIRE_EQ(
        edge_sentinel::ipc::parse_command("inject temperature nan").error,
        ParseError::invalid_number);
    ES_REQUIRE_EQ(
        edge_sentinel::ipc::parse_command("inject temperature 999").error,
        ParseError::out_of_range);
    ES_REQUIRE_EQ(
        edge_sentinel::ipc::parse_command("status extra").error,
        ParseError::wrong_argument_count);

    const std::string oversized(300, 'x');
    ES_REQUIRE_EQ(edge_sentinel::ipc::parse_command(oversized).error, ParseError::too_long);
}

}  // namespace

int main() {
    return edge_sentinel::test::run([] {
        test_simple_commands();
        test_injection_commands();
        test_rejects_untrusted_input();
    });
}
