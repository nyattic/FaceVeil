#include "redactly/VideoProcessor.hpp"

#include <QCoreApplication>

#include <algorithm>

namespace redactly
{
    namespace
    {
        QString trVideoProcessor(const char *text)
        {
            return QCoreApplication::translate("redactly::VideoProcessor", text);
        }

        void scaleTracksToNative(std::vector<Track> &tracks, float scaleX, float scaleY)
        {
            if (scaleX == 1.0F && scaleY == 1.0F)
            {
                return;
            }
            for (auto &track: tracks)
            {
                for (auto &tracked: track.boxes)
                {
                    tracked.box.x *= scaleX;
                    tracked.box.y *= scaleY;
                    tracked.box.width *= scaleX;
                    tracked.box.height *= scaleY;
                }
            }
        }
    }

    VideoProcessResult processVideo(const FfmpegTools &tools,
                                    const QString &sourcePath,
                                    const QString &destinationPath,
                                    const VideoInfo &info,
                                    const VideoProcessOptions &options,
                                    const VideoDetectFn &detect,
                                    const std::atomic<bool> &cancelled,
                                    const VideoProgressFn &progress)
    {
        VideoProcessResult result;

        std::vector<FaceDetections> frameDetections;
        if (info.estimatedFrameCount > 0)
        {
            frameDetections.reserve(static_cast<std::size_t>(info.estimatedFrameCount));
        }

        float scaleX = 1.0F;
        float scaleY = 1.0F;
        {
            VideoFrameReader reader;
            if (!reader.open(tools, sourcePath, info, options.analysisLongEdge))
            {
                result.error = reader.errorString();
                return result;
            }
            scaleX = static_cast<float>(info.displayWidth()) / reader.frameWidth();
            scaleY = static_cast<float>(info.displayHeight()) / reader.frameHeight();

            cv::Mat frame;
            while (reader.readFrame(frame))
            {
                if (cancelled.load(std::memory_order_acquire))
                {
                    result.status = VideoProcessStatus::Cancelled;
                    return result;
                }
                frameDetections.push_back(detect ? detect(frame) : FaceDetections{});
                if (progress)
                {
                    progress(1, static_cast<qint64>(frameDetections.size()),
                             std::max<qint64>(info.estimatedFrameCount,
                                              static_cast<qint64>(frameDetections.size())));
                }
            }
            if (!reader.errorString().isEmpty())
            {
                result.error = reader.errorString();
                return result;
            }
        }

        const auto frameCount = static_cast<qint64>(frameDetections.size());
        result.frameCount = frameCount;
        if (frameCount == 0)
        {
            result.error = trVideoProcessor("No frames could be decoded.");
            return result;
        }

        TrackerConfig trackerConfig = options.tracker;
        trackerConfig.highScoreThreshold = options.scoreThreshold;
        auto tracks = buildBidirectionalTracks(frameDetections, trackerConfig);
        postProcessTracks(tracks, options.postProcess, static_cast<int>(frameCount));
        scaleTracksToNative(tracks, scaleX, scaleY);
        result.trackCount = static_cast<int>(tracks.size());
        frameDetections.clear();
        frameDetections.shrink_to_fit();

        VideoFrameReader reader;
        if (!reader.open(tools, sourcePath, info))
        {
            result.error = reader.errorString();
            return result;
        }
        VideoFrameWriter writer;
        if (!writer.open(tools, destinationPath, sourcePath, info, options.crf))
        {
            result.error = writer.errorString();
            return result;
        }

        qint64 frameIndex = 0;
        cv::Mat frame;
        while (reader.readFrame(frame))
        {
            if (cancelled.load(std::memory_order_acquire))
            {
                writer.abort();
                result.status = VideoProcessStatus::Cancelled;
                return result;
            }

            const auto regions =
                    trackRegionsForFrame(tracks, static_cast<int>(frameIndex));
            FaceDetections toRedact;
            toRedact.reserve(regions.size());
            for (const auto &region: regions)
            {
                toRedact.push_back({region, 1.0F});
            }
            applyAnonymization(frame, toRedact, options.method, options.mosaicBlockSize,
                               options.paddingRatio, options.shape, options.softEdges);

            if (!writer.writeFrame(frame))
            {
                result.error = writer.errorString();
                writer.abort();
                return result;
            }
            ++frameIndex;
            if (progress)
            {
                progress(2, frameIndex, frameCount);
            }
        }
        if (!reader.errorString().isEmpty())
        {
            result.error = reader.errorString();
            writer.abort();
            return result;
        }
        if (frameIndex == 0)
        {
            result.error = trVideoProcessor("No frames could be decoded.");
            writer.abort();
            return result;
        }

        if (!writer.finish())
        {
            result.error = writer.errorString();
            return result;
        }

        result.status = VideoProcessStatus::Completed;
        return result;
    }
}
