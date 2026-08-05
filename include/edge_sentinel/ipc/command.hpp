#pragma once

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace edge_sentinel::ipc {

inline constexpr std::size_t kMaximumCommandLength = 256;

enum class CommandKind {
    invalid,
    status,
    metrics,
    inject,
    clear,
    reset,
    shutdown,
};

enum class InjectionTarget {
    none,
    temperature,
    vibration,
    current,
    offline,
};

enum class ParseError {
    none,
    empty,
    too_long,
    unknown_command,
    wrong_argument_count,
    unknown_target,
    invalid_number,
    out_of_range,
};

struct Command {
    CommandKind kind{CommandKind::invalid};
    InjectionTarget target{InjectionTarget::none};
    double value{0.0};
};

struct ParseResult {
    Command command{};
    ParseError error{ParseError::none};

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == ParseError::none;
    }
};

namespace detail {

[[nodiscard]] constexpr bool is_space(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

[[nodiscard]] constexpr std::string_view trim(std::string_view input) noexcept {
    while (!input.empty() && is_space(input.front())) {
        input.remove_prefix(1);
    }
    while (!input.empty() && is_space(input.back())) {
        input.remove_suffix(1);
    }
    return input;
}

struct Tokens {
    std::array<std::string_view, 4> values{};
    std::size_t count{0};
    bool overflow{false};
};

[[nodiscard]] constexpr Tokens tokenize(std::string_view input) noexcept {
    Tokens tokens;
    while (!input.empty()) {
        while (!input.empty() && is_space(input.front())) {
            input.remove_prefix(1);
        }
        if (input.empty()) {
            break;
        }
        if (tokens.count == tokens.values.size()) {
            tokens.overflow = true;
            break;
        }

        std::size_t length = 0;
        while (length < input.size() && !is_space(input[length])) {
            ++length;
        }
        tokens.values[tokens.count++] = input.substr(0, length);
        input.remove_prefix(length);
    }
    return tokens;
}

[[nodiscard]] inline bool parse_number(std::string_view input, double& output) noexcept {
    const char* begin = input.data();
    const char* end = begin + input.size();
    const auto result = std::from_chars(begin, end, output, std::chars_format::general);
    return result.ec == std::errc{} && result.ptr == end && std::isfinite(output);
}

[[nodiscard]] constexpr bool in_range(InjectionTarget target, double value) noexcept {
    switch (target) {
    case InjectionTarget::temperature:
        return value >= -50.0 && value <= 200.0;
    case InjectionTarget::vibration:
        return value >= 0.0 && value <= 100.0;
    case InjectionTarget::current:
        return value >= 0.0 && value <= 200.0;
    case InjectionTarget::offline:
    case InjectionTarget::none:
        return true;
    }
    return false;
}

}  // namespace detail

[[nodiscard]] inline ParseResult parse_command(std::string_view input) noexcept {
    if (input.size() > kMaximumCommandLength) {
        return ParseResult{{}, ParseError::too_long};
    }

    input = detail::trim(input);
    if (input.empty()) {
        return ParseResult{{}, ParseError::empty};
    }

    const detail::Tokens tokens = detail::tokenize(input);
    if (tokens.overflow) {
        return ParseResult{{}, ParseError::wrong_argument_count};
    }

    const std::string_view verb = tokens.values[0];
    if (verb == "status" || verb == "metrics" || verb == "clear" || verb == "reset" ||
        verb == "shutdown") {
        if (tokens.count != 1) {
            return ParseResult{{}, ParseError::wrong_argument_count};
        }
        if (verb == "status") {
            return ParseResult{Command{CommandKind::status}, ParseError::none};
        }
        if (verb == "metrics") {
            return ParseResult{Command{CommandKind::metrics}, ParseError::none};
        }
        if (verb == "clear") {
            return ParseResult{Command{CommandKind::clear}, ParseError::none};
        }
        if (verb == "reset") {
            return ParseResult{Command{CommandKind::reset}, ParseError::none};
        }
        return ParseResult{Command{CommandKind::shutdown}, ParseError::none};
    }

    if (verb != "inject") {
        return ParseResult{{}, ParseError::unknown_command};
    }
    if (tokens.count < 2) {
        return ParseResult{{}, ParseError::wrong_argument_count};
    }

    InjectionTarget target = InjectionTarget::none;
    if (tokens.values[1] == "temperature") {
        target = InjectionTarget::temperature;
    } else if (tokens.values[1] == "vibration") {
        target = InjectionTarget::vibration;
    } else if (tokens.values[1] == "current") {
        target = InjectionTarget::current;
    } else if (tokens.values[1] == "offline") {
        target = InjectionTarget::offline;
    } else {
        return ParseResult{{}, ParseError::unknown_target};
    }

    if (target == InjectionTarget::offline) {
        if (tokens.count != 2) {
            return ParseResult{{}, ParseError::wrong_argument_count};
        }
        return ParseResult{Command{CommandKind::inject, target, 0.0}, ParseError::none};
    }
    if (tokens.count != 3) {
        return ParseResult{{}, ParseError::wrong_argument_count};
    }

    double value = 0.0;
    if (!detail::parse_number(tokens.values[2], value)) {
        return ParseResult{{}, ParseError::invalid_number};
    }
    if (!detail::in_range(target, value)) {
        return ParseResult{{}, ParseError::out_of_range};
    }
    return ParseResult{Command{CommandKind::inject, target, value}, ParseError::none};
}

[[nodiscard]] constexpr std::string_view to_string(ParseError error) noexcept {
    switch (error) {
    case ParseError::none:
        return "none";
    case ParseError::empty:
        return "empty";
    case ParseError::too_long:
        return "too_long";
    case ParseError::unknown_command:
        return "unknown_command";
    case ParseError::wrong_argument_count:
        return "wrong_argument_count";
    case ParseError::unknown_target:
        return "unknown_target";
    case ParseError::invalid_number:
        return "invalid_number";
    case ParseError::out_of_range:
        return "out_of_range";
    }
    return "unknown";
}

}  // namespace edge_sentinel::ipc
