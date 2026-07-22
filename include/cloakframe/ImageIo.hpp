#pragma once

#include <opencv2/core.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace cloakframe
{
    enum class ImageWriteResult
    {
        Failed,
        Saved,
        SavedWithoutMetadata,
    };

    enum class FileMoveResult
    {
        Moved,
        CrossDevice,
        Failed,
    };

    struct FileIdentity
    {
        std::uint64_t device = 0;
        std::uint64_t file = 0;
        std::uint64_t size = 0;
        std::int64_t modifiedSeconds = 0;
        std::int64_t modifiedNanoseconds = 0;
        std::int64_t changedSeconds = 0;
        std::int64_t changedNanoseconds = 0;

        bool operator==(const FileIdentity &) const = default;
    };

    [[nodiscard]] std::optional<FileIdentity> captureFileIdentity(
        const std::filesystem::path &path);

    bool metadataSupportAvailable();

    int readExifOrientation(const std::filesystem::path &source);

    void applyOrientation(cv::Mat &image, int orientation);

    cv::Mat toDetectionBgr(const cv::Mat &image);

    cv::Mat imreadUnicode(const std::filesystem::path &source, int flags);

    [[nodiscard]] std::size_t imageFrameCount(const std::filesystem::path &source);

    bool imwriteUnicodeNoReplace(const std::filesystem::path &destination,
                                 const cv::Mat &image,
                                 const std::vector<int> &params = {});

    ImageWriteResult imwriteUnicodeNoReplaceAtRoot(
        const std::filesystem::path &outputRoot,
        const std::filesystem::path &relativeDestination,
        const cv::Mat &image,
        const std::vector<int> &params = {},
        const std::filesystem::path &metadataSource = {},
        const std::function<bool()> &publishGuard = {});

    bool copyFileNoReplace(const std::filesystem::path &source,
                           const std::filesystem::path &destination);

    bool copyFileNoReplaceAtRoot(const std::filesystem::path &source,
                                 const std::filesystem::path &outputRoot,
                                 const std::filesystem::path &relativeDestination,
                                 const std::function<bool()> &publishGuard = {});

    FileMoveResult moveFileNoReplaceAtRoot(
        const std::filesystem::path &source,
        const std::filesystem::path &outputRoot,
        const std::filesystem::path &relativeDestination,
        const std::function<bool()> &publishGuard = {});

    std::vector<int> encodeParamsForExtension(const std::string &extLower);

    bool copyMetadata(const std::filesystem::path &source,
                      const std::filesystem::path &destination,
                      bool normalizeOrientation);
}
