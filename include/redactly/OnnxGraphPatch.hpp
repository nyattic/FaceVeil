#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace redactly
{
    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    makeOnnxSpatialDimsFixed(const std::vector<std::uint8_t> &modelBytes, int size);
}
