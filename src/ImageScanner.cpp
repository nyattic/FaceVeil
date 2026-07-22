#include "cloakframe/ImageScanner.hpp"

#include "cloakframe/PathUtil.hpp"
#include "cloakframe/VideoIo.hpp"

#include <QFileInfo>

#include <algorithm>
#include <system_error>
#include <unordered_set>

namespace cloakframe
{
    namespace
    {
        std::string lowercaseExtension(const std::filesystem::path &path)
        {
            auto extension = pathToUtf8(path.extension());
            for (auto &ch: extension)
            {
                if (ch >= 'A' && ch <= 'Z')
                {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }
            return extension;
        }

        bool escapesBase(const std::filesystem::path &relative)
        {
            for (const auto &part: relative)
            {
                if (part == "..")
                {
                    return true;
                }
            }
            return false;
        }

        void appendFile(std::vector<ScanResult> &results,
                        const std::filesystem::path &file,
                        const std::filesystem::path &base,
                        const bool includeVideos)
        {
            if (!isSupportedImage(file) && !(includeVideos && isSupportedVideo(file)))
            {
                return;
            }

            std::error_code error;
            auto relative = std::filesystem::relative(file, base, error);
            if (error || relative.empty() || relative.is_absolute() || escapesBase(relative))
            {
                relative = file.filename();
            }
            results.push_back({file, relative});
        }
    }

    bool isSupportedImage(const std::filesystem::path &path)
    {
        const auto extension = lowercaseExtension(path);
        return extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
               extension == ".bmp" || extension == ".tif" || extension == ".tiff" ||
               extension == ".webp";
    }

    std::vector<ScanResult> scanImages(const QStringList &inputs, bool recursive)
    {
        return scanMedia(inputs, recursive, false);
    }

    std::vector<ScanResult> scanMedia(const QStringList &inputs, bool recursive,
                                      const bool includeVideos)
    {
        std::vector<ScanResult> results;

        std::unordered_set<std::string> visitedCanonical;
        const auto markVisited = [&visitedCanonical](const std::filesystem::path &file) -> bool
        {
            std::error_code ec;
            auto canonical = std::filesystem::canonical(file, ec);
            const auto key = ec ? pathToUtf8(file.lexically_normal()) : pathToUtf8(canonical);
            return visitedCanonical.insert(key).second;
        };

        for (const auto &input: inputs)
        {
            const QFileInfo info(input);
            const auto path = pathFromQString(input);

            if (info.isFile())
            {
                if (markVisited(path))
                {
                    appendFile(results, path, path.parent_path(), includeVideos);
                }
                continue;
            }

            if (!info.isDir())
            {
                continue;
            }

            std::error_code error;

            constexpr auto options = std::filesystem::directory_options::skip_permission_denied;
            if (recursive)
            {
                auto it = std::filesystem::recursive_directory_iterator(path, options, error);
                const auto end = std::filesystem::recursive_directory_iterator{};
                while (!error && it != end)
                {
                    if (it->is_regular_file(error) && markVisited(it->path()))
                    {
                        appendFile(results, it->path(), path, includeVideos);
                    }
                    it.increment(error);
                }
            } else
            {
                for (const auto &entry:
                     std::filesystem::directory_iterator(path, options, error))
                {
                    if (error)
                    {
                        break;
                    }
                    if (entry.is_regular_file() && markVisited(entry.path()))
                    {
                        appendFile(results, entry.path(), path, includeVideos);
                    }
                }
            }
        }

        std::ranges::sort(results, [](const ScanResult &a, const ScanResult &b)
        {
            return pathToUtf8(a.sourcePath) < pathToUtf8(b.sourcePath);
        });

        return results;
    }
}
