#include "redactly/DetectionGeometry.hpp"
#include "redactly/SceneCut.hpp"
#include "redactly/Tracking.hpp"
#include "redactly/VideoIo.hpp"
#include "redactly/VideoProcessor.hpp"
#include "redactly/YunetFaceDetector.hpp"

#include <QCoreApplication>
#include <QString>

#include <opencv2/core.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr float kLargeWidthRatio = 0.20F;
    constexpr float kLargeHeightRatio = 0.30F;

    bool isLargeBox(const cv::Rect2f &box, int frameWidth, int frameHeight)
    {
        return box.width > kLargeWidthRatio * static_cast<float>(frameWidth)
               || box.height > kLargeHeightRatio * static_cast<float>(frameHeight);
    }

    void printTrackedFrame(const std::vector<redactly::Track> &tracks, int frame)
    {
        const redactly::TrackedBox *largest = nullptr;
        int largestId = 0;
        for (const auto &track: tracks)
        {
            if (const auto *box = track.boxAtFrame(frame))
            {
                if (largest == nullptr || box->box.area() > largest->box.area())
                {
                    largest = box;
                    largestId = track.id;
                }
            }
        }
        if (largest == nullptr)
        {
            std::printf("frame %d: no tracked boxes\n", frame);
            return;
        }
        std::printf("frame %d: track %d box=(%.1f, %.1f, %.1f, %.1f) score=%.3f interpolated=%d\n",
                    frame, largestId, largest->box.x, largest->box.y, largest->box.width,
                    largest->box.height, largest->score, largest->interpolated ? 1 : 0);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3)
    {
        std::fprintf(stderr,
                     "usage: %s <model.onnx> <video> [userScoreThreshold=0.5] [keyFrame ...]\n",
                     argv[0]);
        return 2;
    }
    const std::string modelPath = argv[1];
    const QString videoPath = QString::fromLocal8Bit(argv[2]);
    const float userThreshold = argc > 3 ? std::stof(argv[3]) : 0.5F;
    QString renderPath;
    std::vector<int> keyFrames;
    for (int i = 4; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--render" && i + 1 < argc)
        {
            renderPath = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        keyFrames.push_back(std::stoi(argv[i]));
    }

    QString error;
    const auto tools = redactly::locateFfmpegTools(&error);
    if (!tools)
    {
        std::fprintf(stderr, "ffmpeg not found: %s\n", error.toUtf8().constData());
        return 1;
    }
    const auto info = redactly::probeVideo(*tools, videoPath, &error);
    if (!info)
    {
        std::fprintf(stderr, "probe failed: %s\n", error.toUtf8().constData());
        return 1;
    }

    redactly::VideoProcessOptions options;
    options.scoreThreshold = userThreshold;

    if (!renderPath.isEmpty())
    {
        options.method = redactly::AnonymizationMethod::Blur;
        options.shape = redactly::MaskShape::Ellipse;
        options.softEdges = true;
        options.paddingRatio = 0.18F;
        redactly::YunetFaceDetector detector(modelPath, 960, false);
        const float detectionThreshold =
                std::min(options.tracker.lowScoreThreshold, options.scoreThreshold);
        std::atomic<bool> cancelled{false};
        const auto result = redactly::processVideo(
                *tools, videoPath, renderPath, *info, options,
                [&](const cv::Mat &frame)
                {
                    return detector.detect(frame, detectionThreshold, options.nmsThreshold);
                },
                cancelled, nullptr);
        if (result.status != redactly::VideoProcessStatus::Completed)
        {
            std::fprintf(stderr, "render failed: %s\n", result.error.toUtf8().constData());
            return 1;
        }
        std::printf("rendered %s tracks=%d frames=%lld encoder=%s\n",
                    renderPath.toUtf8().constData(), result.trackCount,
                    static_cast<long long>(result.frameCount),
                    result.encoderName.toUtf8().constData());
        return 0;
    }

    redactly::VideoFrameReader reader;
    if (!reader.open(*tools, videoPath, *info, options.analysisLongEdge))
    {
        std::fprintf(stderr, "open failed: %s\n", reader.errorString().toUtf8().constData());
        return 1;
    }

    redactly::YunetFaceDetector detector(modelPath, 960, false);
    const float detectionThreshold =
            std::min(options.tracker.lowScoreThreshold, options.scoreThreshold);

    std::vector<redactly::FaceDetections> frameDetections;
    redactly::SceneCutDetector cutDetector;
    cv::Mat frame;
    std::size_t rawTotal = 0;
    std::size_t largeTotal = 0;
    float largeMaxScore = 0.0F;
    int largeMaxScoreFrame = -1;
    while (reader.readFrame(frame))
    {
        cutDetector.push(frame);
        auto detections = detector.detect(frame, detectionThreshold, options.nmsThreshold);
        const int index = static_cast<int>(frameDetections.size());
        rawTotal += detections.size();
        for (const auto &detection: detections)
        {
            if (isLargeBox(detection.box, reader.frameWidth(), reader.frameHeight()))
            {
                ++largeTotal;
                if (detection.score > largeMaxScore)
                {
                    largeMaxScore = detection.score;
                    largeMaxScoreFrame = index;
                }
            }
        }
        frameDetections.push_back(std::move(detections));
    }
    if (!reader.errorString().isEmpty())
    {
        std::fprintf(stderr, "read failed: %s\n", reader.errorString().toUtf8().constData());
        return 1;
    }

    const int frameCount = static_cast<int>(frameDetections.size());
    std::printf("frames=%d analysis=%dx%d detectionThreshold=%.2f\n", frameCount,
                reader.frameWidth(), reader.frameHeight(), detectionThreshold);
    std::printf("raw detections=%zu\n", rawTotal);
    std::printf("large raw detections=%zu maxScore=%.3f frame=%d\n", largeTotal, largeMaxScore,
                largeMaxScoreFrame);

    const redactly::SceneCuts cuts = cutDetector.finish();
    redactly::TrackerConfig trackerConfig = options.tracker;
    trackerConfig.highScoreThreshold = redactly::videoStrongScoreThreshold(userThreshold);
    trackerConfig.newTrackScoreThreshold = redactly::videoNewTrackScoreThreshold(userThreshold);
    auto tracks = redactly::buildBidirectionalTracks(frameDetections, trackerConfig, 0.5F, cuts);
    redactly::TrackPostProcessConfig postProcess = options.postProcess;
    postProcess.strongScoreThreshold = trackerConfig.highScoreThreshold;
    redactly::postProcessTracks(tracks, postProcess, frameCount, cuts);

    std::size_t trackedBoxes = 0;
    std::size_t largeTrackedBoxes = 0;
    const redactly::TrackedBox *largestTracked = nullptr;
    int largestTrackedId = 0;
    for (const auto &track: tracks)
    {
        for (const auto &tracked: track.boxes)
        {
            ++trackedBoxes;
            if (isLargeBox(tracked.box, reader.frameWidth(), reader.frameHeight()))
            {
                ++largeTrackedBoxes;
            }
            if (largestTracked == nullptr || tracked.box.area() > largestTracked->box.area())
            {
                largestTracked = &tracked;
                largestTrackedId = track.id;
            }
        }
    }
    std::printf("tracks=%zu trackedBoxes=%zu largeTrackedBoxes=%zu\n", tracks.size(),
                trackedBoxes, largeTrackedBoxes);
    if (largestTracked != nullptr)
    {
        std::printf("largest tracked: frame %d track %d box=(%.1f, %.1f, %.1f, %.1f) "
                    "score=%.3f interpolated=%d\n",
                    largestTracked->frame, largestTrackedId, largestTracked->box.x,
                    largestTracked->box.y, largestTracked->box.width, largestTracked->box.height,
                    largestTracked->score, largestTracked->interpolated ? 1 : 0);
    }
    for (const int keyFrame: keyFrames)
    {
        printTrackedFrame(tracks, keyFrame);
    }

    constexpr float kConfidentScore = 0.7F;
    constexpr float kCoverageIou = 0.3F;
    std::size_t confident = 0;
    std::size_t uncovered = 0;
    for (int index = 0; index < frameCount; ++index)
    {
        const auto regions = redactly::trackRegionsForFrame(tracks, index);
        for (const auto &detection: frameDetections[static_cast<std::size_t>(index)])
        {
            if (detection.score < kConfidentScore)
            {
                continue;
            }
            ++confident;
            float bestIou = 0.0F;
            for (const auto &region: regions)
            {
                bestIou = std::max(bestIou,
                                   redactly::intersectionOverUnion(region, detection.box));
            }
            if (bestIou < kCoverageIou)
            {
                ++uncovered;
                if (uncovered <= 10)
                {
                    std::printf("uncovered confident detection: frame %d box=(%.1f, %.1f, "
                                "%.1f, %.1f) score=%.3f\n",
                                index, detection.box.x, detection.box.y, detection.box.width,
                                detection.box.height, detection.score);
                }
            }
        }
    }
    std::printf("confident detections=%zu uncovered=%zu\n", confident, uncovered);
    return 0;
}
