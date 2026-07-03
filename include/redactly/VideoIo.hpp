#pragma once

#include <QString>

#include <opencv2/core.hpp>

#include <filesystem>
#include <memory>
#include <optional>

class QProcess;

namespace redactly
{
    struct FfmpegTools
    {
        QString ffmpegPath;
        QString ffprobePath;
        bool bundled = false;
        QString versionLine;
    };

    std::optional<FfmpegTools> locateFfmpegTools(QString *error = nullptr);

    bool isSupportedVideo(const std::filesystem::path &path);

    enum class VideoQuality
    {
        HighQuality,
        Balanced,
        SpaceSaver,
    };

    int crfForQuality(VideoQuality quality);

    struct VideoInfo
    {
        int width = 0;
        int height = 0;
        int rotation = 0;
        int fpsNum = 0;
        int fpsDen = 1;
        double durationSeconds = 0.0;
        qint64 estimatedFrameCount = 0;
        bool hasAudio = false;
        bool isVfr = false;
        QString videoCodec;
        QString audioCodec;
        QString pixelFormat;
        QString colorTransfer;

        [[nodiscard]] int displayWidth() const;
        [[nodiscard]] int displayHeight() const;
        [[nodiscard]] double fps() const;
    };

    std::optional<VideoInfo> probeVideo(const FfmpegTools &tools,
                                        const QString &path,
                                        QString *error = nullptr);

    QString videoUnsupportedReason(const VideoInfo &info);

    class VideoFrameReader
    {
    public:
        VideoFrameReader();
        ~VideoFrameReader();

        VideoFrameReader(const VideoFrameReader &) = delete;
        VideoFrameReader &operator=(const VideoFrameReader &) = delete;

        bool open(const FfmpegTools &tools, const QString &path, const VideoInfo &info,
                  int decodeLongEdge = 0);
        bool readFrame(cv::Mat &frame);
        void close();

        [[nodiscard]] int frameWidth() const;
        [[nodiscard]] int frameHeight() const;

        [[nodiscard]] bool atEnd() const;
        [[nodiscard]] QString errorString() const;

    private:
        std::unique_ptr<QProcess> process_;
        int frameWidth_ = 0;
        int frameHeight_ = 0;
        bool atEnd_ = false;
        QString error_;
    };

    class VideoFrameWriter
    {
    public:
        VideoFrameWriter();
        ~VideoFrameWriter();

        VideoFrameWriter(const VideoFrameWriter &) = delete;
        VideoFrameWriter &operator=(const VideoFrameWriter &) = delete;

        bool open(const FfmpegTools &tools,
                  const QString &destination,
                  const QString &audioSource,
                  const VideoInfo &info,
                  int crf);
        bool writeFrame(const cv::Mat &frame);
        bool finish();
        void abort();

        [[nodiscard]] QString errorString() const;

    private:
        std::unique_ptr<QProcess> process_;
        QString tempPath_;
        QString destinationPath_;
        int frameWidth_ = 0;
        int frameHeight_ = 0;
        QString error_;
    };
}
