#pragma once

#include <cstdint>

namespace cloakframe
{
    [[nodiscard]] std::uint64_t physicalMemoryBytes() noexcept;

    [[nodiscard]] std::uint64_t adaptiveMemoryBudget(std::uint64_t minimum,
                                                     std::uint64_t maximum,
                                                     std::uint64_t divisor) noexcept;
}
