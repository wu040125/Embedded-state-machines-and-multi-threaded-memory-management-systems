#include "edge_sentinel/version.hpp"

#include <iostream>

int main() {
    std::cout << edge_sentinel::kProjectName << " " << edge_sentinel::kVersion
              << " - runtime bootstrap complete\n";
    return 0;
}
