#pragma once

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace edge_sentinel::test {

class Failure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

inline void require(bool condition, const char* expression, const char* file, int line) {
    if (condition) {
        return;
    }

    std::ostringstream message;
    message << file << ':' << line << ": REQUIRE(" << expression << ") failed";
    throw Failure(message.str());
}

template <typename Actual, typename Expected>
void require_equal(
    const Actual& actual,
    const Expected& expected,
    const char* actual_expression,
    const char* expected_expression,
    const char* file,
    int line) {
    if (actual == expected) {
        return;
    }

    std::ostringstream message;
    message << file << ':' << line << ": REQUIRE_EQ(" << actual_expression << ", "
            << expected_expression << ") failed";
    throw Failure(message.str());
}

template <typename TestFunction>
int run(TestFunction&& test_function) {
    try {
        test_function();
        std::cout << "PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

}  // namespace edge_sentinel::test

#define ES_REQUIRE(expression) \
    ::edge_sentinel::test::require((expression), #expression, __FILE__, __LINE__)

#define ES_REQUIRE_EQ(actual, expected)                                               \
    ::edge_sentinel::test::require_equal(                                             \
        (actual), (expected), #actual, #expected, __FILE__, __LINE__)
