#pragma once

#include <filesystem>
#include <system_error>

namespace cloakframe
{
    inline bool isWithinRoot(const std::filesystem::path &candidate,
                             const std::filesystem::path &root)
    {
        std::error_code ec;
        const auto relative = std::filesystem::relative(candidate, root, ec);
        if (ec || relative.empty())
        {
            return false;
        }
        const auto first = relative.begin();
        return first != relative.end() && *first != "..";
    }

    inline bool destinationIsSafe(const std::filesystem::path &destination,
                                  const std::filesystem::path &safeRoot)
    {
        const auto lexicalDestination = destination.lexically_normal();
        const auto lexicalRoot = safeRoot.lexically_normal();
        if (!(isWithinRoot(lexicalDestination, lexicalRoot) || lexicalDestination == lexicalRoot))
        {
            return false;
        }

        std::error_code ec;
        const auto canonicalRoot = std::filesystem::weakly_canonical(safeRoot, ec);
        if (ec)
        {
            return false;
        }

        auto current = lexicalDestination;
        while (!current.empty() && current != current.root_path())
        {
            ec.clear();
            const auto status = std::filesystem::symlink_status(current, ec);
            if (ec)
            {
                if (ec == std::errc::no_such_file_or_directory ||
                    ec == std::errc::not_a_directory)
                {
                    current = current.parent_path();
                    continue;
                }
                return false;
            }

            if (std::filesystem::is_symlink(status))
            {
                return false;
            }

            if (std::filesystem::exists(status))
            {
                const auto resolved = std::filesystem::canonical(current, ec);
                return !ec &&
                       (resolved == canonicalRoot || isWithinRoot(resolved, canonicalRoot));
            }
            current = current.parent_path();
        }
        return false;
    }
}
