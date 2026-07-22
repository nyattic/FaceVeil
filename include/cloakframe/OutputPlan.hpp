#pragma once

#include "cloakframe/ImageScanner.hpp"

#include <cstddef>
#include <filesystem>
#include <vector>

namespace cloakframe
{
    struct OutputConflict
    {
        enum class Kind
        {
            DuplicateDestination,
            ExistingDestination,
        };

        Kind kind;
        std::filesystem::path source;
        std::filesystem::path otherSource;
        std::filesystem::path destination;
    };

    [[nodiscard]] std::filesystem::path outputRelativePath(const ScanResult &item);

    [[nodiscard]] std::vector<OutputConflict> findOutputConflicts(
        const std::vector<ScanResult> &items,
        const std::filesystem::path &outputRoot,
        std::size_t limit = 10);
}
