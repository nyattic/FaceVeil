#include "cloakframe/OutputPlan.hpp"

#include "cloakframe/PathUtil.hpp"
#include "cloakframe/VideoIo.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <unordered_map>

namespace cloakframe
{
    namespace
    {
        std::string destinationKey(const std::filesystem::path &path)
        {
            auto key = pathToUtf8(path.lexically_normal());
#if defined(_WIN32) || defined(__APPLE__)
            std::ranges::transform(key, key.begin(), [](unsigned char ch)
            {
                return static_cast<char>(std::tolower(ch));
            });
#endif
            return key;
        }
    }

    std::filesystem::path outputRelativePath(const ScanResult &item)
    {
        if (isSupportedVideo(item.sourcePath))
        {
            auto relative = item.relativePath;
            relative.replace_extension(".mp4");
            return relative;
        }
        return item.relativePath;
    }

    std::vector<OutputConflict> findOutputConflicts(
        const std::vector<ScanResult> &items,
        const std::filesystem::path &outputRoot,
        const std::size_t limit)
    {
        std::vector<OutputConflict> conflicts;
        std::unordered_map<std::string, std::filesystem::path> firstSourceForDestination;

        for (const auto &item: items)
        {
            const auto destination = (outputRoot / outputRelativePath(item)).lexically_normal();
            const auto [it, inserted] = firstSourceForDestination.emplace(
                destinationKey(destination), item.sourcePath);
            if (!inserted)
            {
                conflicts.push_back({OutputConflict::Kind::DuplicateDestination,
                                     item.sourcePath, it->second, destination});
            }

            std::error_code statusError;
            const auto destinationStatus = std::filesystem::symlink_status(destination,
                                                                            statusError);
            if (!statusError && (std::filesystem::exists(destinationStatus) ||
                                 std::filesystem::is_symlink(destinationStatus)))
            {
                conflicts.push_back({OutputConflict::Kind::ExistingDestination,
                                     item.sourcePath, {}, destination});
            }

            if (conflicts.size() >= limit)
            {
                break;
            }
        }
        return conflicts;
    }
}
