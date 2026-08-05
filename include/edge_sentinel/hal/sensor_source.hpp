#pragma once

#include <edge_sentinel/domain/event.hpp>

namespace edge_sentinel::hal {

class ISensorSource {
public:
    virtual ~ISensorSource() = default;

    [[nodiscard]] virtual domain::SensorSample read() = 0;
};

}  // namespace edge_sentinel::hal
