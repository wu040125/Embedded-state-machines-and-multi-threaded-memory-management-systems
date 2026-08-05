#include "edge_sentinel/version.hpp"
#include "test_support.hpp"

#include <string_view>

int main() {
    return edge_sentinel::test::run([] {
        ES_REQUIRE_EQ(edge_sentinel::kProjectName, std::string_view{"EdgeSentinel"});
        ES_REQUIRE(!edge_sentinel::kVersion.empty());
    });
}
