#pragma once

#include <QString>
#include <QStringList>

#include <filesystem>
#include <vector>

namespace cloakframe
{
    struct ScanResult
    {
        std::filesystem::path sourcePath;
        std::filesystem::path relativePath;
    };

    std::vector<ScanResult> scanImages(const QStringList &inputs, bool recursive);

    std::vector<ScanResult> scanMedia(const QStringList &inputs, bool recursive,
                                      bool includeVideos);

    bool isSupportedImage(const std::filesystem::path &path);
}
