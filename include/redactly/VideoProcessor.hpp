#pragma once

#include "redactly/FaceDetection.hpp"
#include "redactly/Mosaic.hpp"
#include "redactly/Tracking.hpp"
#include "redactly/VideoIo.hpp"

#include <QString>

#include <atomic>
#include <functional>

namespace redactly
{
    struct VideoProcessOptions
    {
        float scoreThreshold = 0.5F;
        float nmsThreshold = 0.4F;
        int mosaicBlockSize = 14;
        float paddingRatio = 0.18F;
        AnonymizationMethod method = AnonymizationMethod::Mosaic;
        MaskShape shape = MaskShape::Rectangle;
        bool softEdges = false;
        int crf = 18;
        int analysisLongEdge = 960;
        TrackerConfig tracker;
        TrackPostProcessConfig postProcess;
    };

    enum class VideoProcessStatus
    {
        Completed,
        Cancelled,
        Failed,
    };

    struct VideoProcessResult
    {
        VideoProcessStatus status = VideoProcessStatus::Failed;
        QString error;
        qint64 frameCount = 0;
        int trackCount = 0;
    };

    using VideoProgressFn = std::function<void(int pass, qint64 frame, qint64 totalEstimate)>;
    using VideoDetectFn = std::function<FaceDetections(const cv::Mat &frame)>;

    VideoProcessResult processVideo(const FfmpegTools &tools,
                                    const QString &sourcePath,
                                    const QString &destinationPath,
                                    const VideoInfo &info,
                                    const VideoProcessOptions &options,
                                    const VideoDetectFn &detect,
                                    const std::atomic<bool> &cancelled,
                                    const VideoProgressFn &progress = {});
}
