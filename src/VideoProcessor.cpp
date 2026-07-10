#include "redactly/VideoProcessor.hpp"

#include <QCoreApplication>

#include <spdlog/spdlog.h>

#include <opencv2/core.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

namespace redactly
{
    namespace
    {
        class StageTimer
        {
        public:
            using Clock = std::chrono::steady_clock;

            static Clock::time_point now()
            {
                return Clock::now();
            }

            void add(Clock::time_point since)
            {
                total_ += Clock::now() - since;
            }

            [[nodiscard]] long long ms() const
            {
                return std::chrono::duration_cast<std::chrono::milliseconds>(total_).count();
            }

        private:
            Clock::duration total_{};
        };

        class ScopedCvThreads
        {
        public:
            explicit ScopedCvThreads(int threads)
                : previous_(cv::getNumThreads())
            {
                cv::setNumThreads(threads);
            }

            ~ScopedCvThreads()
            {
                cv::setNumThreads(previous_);
            }

            ScopedCvThreads(const ScopedCvThreads &) = delete;
            ScopedCvThreads &operator=(const ScopedCvThreads &) = delete;

        private:
            int previous_;
        };

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

    float videoStrongScoreThreshold(float scoreThreshold)
    {
        // A low user threshold may supply weak candidates to continue an existing track, but
        // those candidates must not create and preserve thousands of false-positive tracks.
        return std::max(0.35F, scoreThreshold - 0.1F);
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
        SceneCutDetector cutDetector;
        {
            VideoFrameReader reader;
            if (!reader.open(tools, sourcePath, info, options.analysisLongEdge))
            {
                result.error = reader.errorString();
                return result;
            }
            scaleX = static_cast<float>(info.displayWidth()) / reader.frameWidth();
            scaleY = static_cast<float>(info.displayHeight()) / reader.frameHeight();

            StageTimer readTimer;
            StageTimer detectTimer;
            cv::Mat frame;
            for (;;)
            {
                const auto readMark = StageTimer::now();
                const bool got = reader.readFrame(frame);
                readTimer.add(readMark);
                if (!got)
                {
                    break;
                }
                if (cancelled.load(std::memory_order_acquire))
                {
                    result.status = VideoProcessStatus::Cancelled;
                    return result;
                }
                const auto detectMark = StageTimer::now();
                cutDetector.push(frame);
                frameDetections.push_back(detect ? detect(frame) : FaceDetections{});
                detectTimer.add(detectMark);
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
            spdlog::info("Pass 1 timing: decode {} ms, detect+scene {} ms ({} frames)",
                         readTimer.ms(), detectTimer.ms(), frameDetections.size());
        }

        const auto frameCount = static_cast<qint64>(frameDetections.size());
        result.frameCount = frameCount;
        if (frameCount == 0)
        {
            result.error = trVideoProcessor("No frames could be decoded.");
            return result;
        }

        const SceneCuts sceneCuts = cutDetector.finish();
        spdlog::info("Video scene cuts detected: {}", sceneCuts.frames().size());

        TrackerConfig trackerConfig = options.tracker;
        trackerConfig.highScoreThreshold = videoStrongScoreThreshold(options.scoreThreshold);
        auto tracks = buildBidirectionalTracks(frameDetections, trackerConfig, 0.5F, sceneCuts);
        TrackPostProcessConfig postProcess = options.postProcess;
        postProcess.strongScoreThreshold = trackerConfig.highScoreThreshold;
        postProcessTracks(tracks, postProcess, static_cast<int>(frameCount), sceneCuts);
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
        if (!writer.open(tools, destinationPath, sourcePath, info, options.crf,
                         options.hardwareEncoder))
        {
            result.error = writer.errorString();
            return result;
        }
        result.encoderName = writer.encoderName();

        StageTimer readTimer;
        StageTimer maskTimer;
        StageTimer writeTimer;
        const int workerCount =
                std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
        const int batchCap = workerCount * 4;
        const ScopedCvThreads maskThreads(1);
        qint64 frameIndex = 0;
        std::vector<cv::Mat> batch;
        batch.reserve(static_cast<std::size_t>(batchCap));
        for (;;)
        {
            batch.clear();
            const auto readMark = StageTimer::now();
            for (int slot = 0; slot < batchCap; ++slot)
            {
                cv::Mat frame;
                if (!reader.readFrame(frame))
                {
                    break;
                }
                batch.push_back(std::move(frame));
            }
            readTimer.add(readMark);
            if (batch.empty())
            {
                break;
            }
            if (cancelled.load(std::memory_order_acquire))
            {
                writer.abort();
                result.status = VideoProcessStatus::Cancelled;
                return result;
            }

            const auto maskMark = StageTimer::now();
            const qint64 baseIndex = frameIndex;
            std::atomic<int> nextSlot{0};
            std::atomic<bool> maskFailed{false};
            const auto maskWorker = [&]()
            {
                int slot;
                while ((slot = nextSlot.fetch_add(1, std::memory_order_relaxed)) <
                       static_cast<int>(batch.size()))
                {
                    try
                    {
                        const auto regions = trackRegionsForFrame(
                                tracks, static_cast<int>(baseIndex + slot));
                        FaceDetections toRedact;
                        toRedact.reserve(regions.size());
                        for (const auto &region: regions)
                        {
                            toRedact.push_back({region, 1.0F});
                        }
                        applyAnonymization(batch[static_cast<std::size_t>(slot)], toRedact,
                                           options.method, options.mosaicBlockSize,
                                           options.paddingRatio, options.shape,
                                           options.softEdges);
                    }
                    catch (...)
                    {
                        maskFailed.store(true, std::memory_order_relaxed);
                    }
                }
            };
            std::vector<std::thread> pool;
            const int helpers =
                    std::min(workerCount, static_cast<int>(batch.size())) - 1;
            pool.reserve(static_cast<std::size_t>(std::max(0, helpers)));
            for (int w = 0; w < helpers; ++w)
            {
                pool.emplace_back(maskWorker);
            }
            maskWorker();
            for (auto &worker: pool)
            {
                worker.join();
            }
            maskTimer.add(maskMark);
            if (maskFailed.load(std::memory_order_relaxed))
            {
                result.error = trVideoProcessor("Video redaction failed.");
                writer.abort();
                return result;
            }

            const auto writeMark = StageTimer::now();
            for (auto &frame: batch)
            {
                if (!writer.writeFrame(frame))
                {
                    result.error = writer.errorString();
                    writer.abort();
                    writeTimer.add(writeMark);
                    return result;
                }
                ++frameIndex;
                if (progress)
                {
                    progress(2, frameIndex, frameCount);
                }
            }
            writeTimer.add(writeMark);
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

        StageTimer finishTimer;
        const auto finishMark = StageTimer::now();
        const bool finished = writer.finish();
        finishTimer.add(finishMark);
        if (!finished)
        {
            result.error = writer.errorString();
            return result;
        }

        spdlog::info("Pass 2 timing: decode {} ms, redact {} ms, encode-write {} ms, "
                     "finalize {} ms ({} frames)",
                     readTimer.ms(), maskTimer.ms(), writeTimer.ms(), finishTimer.ms(),
                     frameIndex);

        result.status = VideoProcessStatus::Completed;
        return result;
    }
}
