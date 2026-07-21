#include "redactly/VideoIo.hpp"
#include "redactly/VideoProcessor.hpp"

#include <atomic>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <opencv2/core.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace
{
    constexpr int kSkipExitCode = 77;

    bool generateSample(const redactly::FfmpegTools &tools, const QString &path,
                        int durationSeconds = 2)
    {
        const QString duration = QString::number(durationSeconds);
        QProcess process;
        process.start(tools.ffmpegPath,
                      {"-v", "error", "-y",
                       "-f", "lavfi", "-i", "testsrc2=size=320x240:rate=30:duration=" + duration,
                       "-f", "lavfi", "-i", "sine=frequency=440:duration=" + duration,
                       "-metadata", "title=RedactlyTestTitle",
                       "-metadata", "location=+37.5665+126.9780/",
                       "-c:v", "libx264", "-pix_fmt", "yuv420p",
                       "-c:a", "aac", "-shortest", path});
        if (!process.waitForStarted(15000) || !process.waitForFinished(60000))
        {
            process.kill();
            return false;
        }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }

    QString rawProbeOutput(const redactly::FfmpegTools &tools, const QString &path)
    {
        QProcess process;
        process.start(tools.ffprobePath,
                      {"-v", "error", "-print_format", "json",
                       "-show_streams", "-show_format", path});
        process.waitForStarted(15000);
        process.waitForFinished(60000);
        return QString::fromUtf8(process.readAllStandardOutput());
    }

    void testUnsupportedReasons()
    {
        redactly::VideoInfo info;
        info.width = 1920;
        info.height = 1080;
        info.fpsNum = 30;
        info.fpsDen = 1;
        info.videoCodec = "h264";
        info.pixelFormat = "yuv420p";
        assert(redactly::videoUnsupportedReason(info).isEmpty());

        info.width = 3840;
        info.height = 2160;
        info.fpsNum = 60;
        assert(redactly::videoUnsupportedReason(info).isEmpty());
        info.width = 1920;
        info.height = 1080;
        info.fpsNum = 30;

        info.videoCodec = "hevc";
        assert(redactly::videoUnsupportedReason(info).isEmpty());

        info.videoCodec = "vp9";
        assert(!redactly::videoUnsupportedReason(info).isEmpty());
        info.videoCodec = "h264";

        info.pixelFormat = "yuv420p10le";
        assert(!redactly::videoUnsupportedReason(info).isEmpty());
        info.pixelFormat = "yuv420p";

        info.colorTransfer = "smpte2084";
        assert(!redactly::videoUnsupportedReason(info).isEmpty());
        info.colorTransfer.clear();

        info.fpsNum = 0;
        assert(!redactly::videoUnsupportedReason(info).isEmpty());

        info.fpsNum = 30;
        info.width = redactly::kMaxVideoDimension + 1;
        assert(!redactly::videoUnsupportedReason(info).isEmpty());
        info.width = 1920;

        info.fpsNum = 241;
        assert(!redactly::videoUnsupportedReason(info).isEmpty());
        info.fpsNum = 30;

        info.durationSeconds = redactly::kMaxVideoDurationSeconds + 1.0;
        assert(!redactly::videoUnsupportedReason(info).isEmpty());
        info.durationSeconds = 0.0;

        info.estimatedFrameCount = redactly::kMaxVideoFrameCount + 1;
        assert(!redactly::videoUnsupportedReason(info).isEmpty());
    }

    void testSupportedExtensions()
    {
        assert(redactly::isSupportedVideo("clip.mp4"));
        assert(redactly::isSupportedVideo("CLIP.MOV"));
        assert(redactly::isSupportedVideo("clip.m4v"));
        assert(!redactly::isSupportedVideo("clip.webm"));
        assert(!redactly::isSupportedVideo("clip.mkv"));
        assert(!redactly::isSupportedVideo("clip.jpg"));
    }

    void testCrfPresets()
    {
        assert(redactly::crfForQuality(redactly::VideoQuality::HighQuality) == 18);
        assert(redactly::crfForQuality(redactly::VideoQuality::Balanced) == 21);
        assert(redactly::crfForQuality(redactly::VideoQuality::SpaceSaver) == 24);
    }

    void testWeakVideoDetectionsCannotBecomeStrongTracks()
    {
        const auto close = [](float actual, float expected)
        {
            return std::abs(actual - expected) < 0.0001F;
        };
        assert(close(redactly::videoStrongScoreThreshold(0.05F), 0.35F));
        assert(close(redactly::videoStrongScoreThreshold(0.20F), 0.35F));
        assert(close(redactly::videoStrongScoreThreshold(0.50F), 0.40F));
        assert(close(redactly::videoStrongScoreThreshold(0.90F), 0.80F));
    }

    void testVideoMaskingPlanIsBoundedByWorkersAndBytes()
    {
        constexpr qint64 constrainedBudget = 256LL * 1024 * 1024;
        constexpr qint64 performanceBudget = 1024LL * 1024 * 1024;
        constexpr qint64 headroom = 48LL * 1024 * 1024;

        const auto fullHd = redactly::videoMaskingPlan(1920, 1080, 64,
                                                       constrainedBudget);
        assert(fullHd.workerCount <= 8);
        assert(fullHd.batchFrames <= 16);
        assert(fullHd.batchFrames * fullHd.frameBytes + headroom <= constrainedBudget);

        const auto constrainedFourK = redactly::videoMaskingPlan(
            3840, 2160, 64, constrainedBudget);
        assert(constrainedFourK.workerCount == 3);
        assert(constrainedFourK.batchFrames == 3);
        assert(constrainedFourK.batchFrames * constrainedFourK.frameBytes + headroom <=
               constrainedBudget);

        const auto performanceFourK = redactly::videoMaskingPlan(
            3840, 2160, 64, performanceBudget);
        assert(performanceFourK.workerCount == 8);
        assert(performanceFourK.batchFrames == 15);
        assert(performanceFourK.batchFrames * performanceFourK.frameBytes + headroom <=
               performanceBudget);

        const auto eightK = redactly::videoMaskingPlan(7680, 4320, 64,
                                                       performanceBudget);
        assert(eightK.workerCount == 3);
        assert(eightK.batchFrames == 3);
        assert(eightK.batchFrames * eightK.frameBytes + headroom <= performanceBudget);

        const auto unknownCpuCount = redactly::videoMaskingPlan(
            320, 240, 0, constrainedBudget);
        assert(unknownCpuCount.workerCount == 1);
        assert(unknownCpuCount.batchFrames == 2);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testUnsupportedReasons();
    std::puts("unsupported reasons: ok");
    testSupportedExtensions();
    std::puts("supported extensions: ok");
    testCrfPresets();
    std::puts("crf presets: ok");
    testWeakVideoDetectionsCannotBecomeStrongTracks();
    std::puts("video strong-score floor: ok");
    testVideoMaskingPlanIsBoundedByWorkersAndBytes();
    std::puts("bounded video masking plan: ok");

    QString locateError;
    const auto tools = redactly::locateFfmpegTools(&locateError);
    if (!tools)
    {
        std::printf("SKIP: %s\n", locateError.toUtf8().constData());
        return kSkipExitCode;
    }
    std::printf("using %s\n", tools->versionLine.toUtf8().constData());

    QTemporaryDir tempDir;
    assert(tempDir.isValid());
    const QString samplePath = tempDir.filePath("sample.mp4");
    const QString outputPath = tempDir.filePath("out/redacted.mp4");
    QDir().mkpath(tempDir.filePath("out"));
    const auto stagingLeftovers = [](const QString &dirPath)
    {
        return QDir(dirPath)
            .entryList({QStringLiteral(".redactly-*")},
                       QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
            .size();
    };

    assert(generateSample(*tools, samplePath));
    std::puts("sample generated: ok");

    QString probeError;
    const auto info = redactly::probeVideo(*tools, samplePath, &probeError);
    assert(info.has_value());
    assert(info->width == 320);
    assert(info->height == 240);
    assert(info->fpsNum == 30 && info->fpsDen == 1);
    assert(info->videoCodec == "h264");
    assert(info->hasAudio);
    assert(info->audioCodec == "aac");
    assert(!info->isVfr);
    assert(redactly::videoUnsupportedReason(*info).isEmpty());
    assert(info->estimatedFrameCount >= 55 && info->estimatedFrameCount <= 65);
    std::puts("probe: ok");

    redactly::VideoFrameReader reader;
    assert(reader.open(*tools, samplePath, *info));

    redactly::VideoFrameWriter writer;
    assert(writer.open(*tools, outputPath, samplePath, *info,
                       redactly::crfForQuality(redactly::VideoQuality::HighQuality)));

    int frameCount = 0;
    cv::Mat frame;
    while (reader.readFrame(frame))
    {
        cv::bitwise_not(frame, frame);
        assert(writer.writeFrame(frame));
        ++frameCount;
    }
    assert(reader.errorString().isEmpty());
    assert(frameCount >= 55 && frameCount <= 65);
    assert(writer.finish());
    assert(stagingLeftovers(tempDir.filePath("out")) == 0);
    std::printf("round trip (%d frames): ok\n", frameCount);

    {
        const QString guardedPath = tempDir.filePath("out/guarded.mp4");
        redactly::VideoFrameWriter guardedWriter;
        assert(guardedWriter.open(*tools, guardedPath, samplePath, *info,
                                  redactly::crfForQuality(
                                      redactly::VideoQuality::SpaceSaver),
                                  false));
        redactly::VideoFrameReader guardedReader;
        assert(guardedReader.open(*tools, samplePath, *info));
        cv::Mat guardedFrame;
        assert(guardedReader.readFrame(guardedFrame));
        assert(guardedWriter.writeFrame(guardedFrame));
        assert(!guardedWriter.finish([] { return false; }));
        assert(!QFile::exists(guardedPath));
        assert(stagingLeftovers(tempDir.filePath("out")) == 0);
        std::puts("video publish guard: ok");
    }

    const auto outInfo = redactly::probeVideo(*tools, outputPath, &probeError);
    assert(outInfo.has_value());
    assert(outInfo->width == 320);
    assert(outInfo->height == 240);
    assert(outInfo->videoCodec == "h264");
    assert(outInfo->hasAudio);
    assert(outInfo->audioCodec == "aac");
    assert(outInfo->durationSeconds > 1.5 && outInfo->durationSeconds < 2.5);

    const QString rawOutput = rawProbeOutput(*tools, outputPath);
    assert(!rawOutput.contains("RedactlyTestTitle"));
    assert(!rawOutput.contains("37.5665"));
    std::puts("output verification: ok");

    {
        redactly::VideoInfo fourKInfo = *info;
        fourKInfo.width = 3840;
        fourKInfo.height = 2160;
        fourKInfo.rotation = 0;
        fourKInfo.fpsNum = 60;
        fourKInfo.fpsDen = 1;
        fourKInfo.durationSeconds = 0.05;
        fourKInfo.estimatedFrameCount = 3;

        const QString fourKPath = tempDir.filePath("out/4k60-source.mp4");
        redactly::VideoFrameWriter fourKWriter;
        assert(fourKWriter.open(
            *tools, fourKPath, samplePath, fourKInfo,
            redactly::crfForQuality(redactly::VideoQuality::SpaceSaver), false));
        cv::Mat fourKFrame(2160, 3840, CV_8UC3);
        for (int frameIndex = 0; frameIndex < 3; ++frameIndex)
        {
            fourKFrame.setTo(cv::Scalar(20 + frameIndex * 30,
                                        40 + frameIndex * 20,
                                        60 + frameIndex * 10));
            assert(fourKWriter.writeFrame(fourKFrame));
        }
        assert(fourKWriter.finish());

        const auto fourKProbe = redactly::probeVideo(*tools, fourKPath, &probeError);
        assert(fourKProbe.has_value());
        assert(fourKProbe->displayWidth() == 3840);
        assert(fourKProbe->displayHeight() == 2160);
        assert(fourKProbe->fpsNum == 60 && fourKProbe->fpsDen == 1);
        assert(redactly::videoUnsupportedReason(*fourKProbe).isEmpty());

        const QString fourKOutput = tempDir.filePath("out/4k60-processed.mp4");
        redactly::VideoProcessOptions fourKOptions;
        fourKOptions.hardwareEncoder = false;
        std::atomic<bool> fourKCancelled{false};
        const auto fourKResult = redactly::processVideo(
            *tools, fourKPath, fourKOutput, *fourKProbe, fourKOptions, {},
            fourKCancelled);
        assert(fourKResult.status == redactly::VideoProcessStatus::Completed);
        assert(fourKResult.frameCount == 3);
        const auto processedFourK = redactly::probeVideo(
            *tools, fourKOutput, &probeError);
        assert(processedFourK.has_value());
        assert(processedFourK->displayWidth() == 3840);
        assert(processedFourK->displayHeight() == 2160);
        assert(processedFourK->fpsNum == 60 && processedFourK->fpsDen == 1);
        std::puts("4K60 end-to-end: ok");
    }

    {
        QProcess encoders;
        encoders.start(tools->ffmpegPath, {"-v", "error", "-encoders"});
        encoders.waitForStarted(15000);
        encoders.waitForFinished(60000);
        const QString encoderList = QString::fromUtf8(encoders.readAllStandardOutput());
        if (encoderList.contains("libx265"))
        {
            const QString hevcPath = tempDir.filePath("out/hevc.mp4");
            redactly::VideoFrameReader hevcReader;
            assert(hevcReader.open(*tools, samplePath, *info));
            redactly::VideoFrameWriter hevcWriter;
            assert(hevcWriter.open(*tools, hevcPath, samplePath, *info,
                                   redactly::crfForQuality(redactly::VideoQuality::Balanced),
                                   false, redactly::VideoCodec::Hevc));
            assert(hevcWriter.encoderName() == "libx265");
            cv::Mat hevcFrame;
            while (hevcReader.readFrame(hevcFrame))
            {
                assert(hevcWriter.writeFrame(hevcFrame));
            }
            assert(hevcWriter.finish());

            const auto hevcInfo = redactly::probeVideo(*tools, hevcPath, &probeError);
            assert(hevcInfo.has_value());
            assert(hevcInfo->videoCodec == "hevc");
            assert(hevcInfo->hasAudio);
            assert(rawProbeOutput(*tools, hevcPath).contains("hvc1"));
            std::puts("hevc round trip: ok");
        }
        else
        {
            std::puts("hevc round trip: skipped (libx265 unavailable)");
        }
    }

    {
        const QString existingPath = tempDir.filePath("out/existing.mp4");
        QFile existing(existingPath);
        assert(existing.open(QIODevice::WriteOnly));
        assert(existing.write("keep-existing-output") == 20);
        existing.close();

        std::atomic<bool> cancelled{false};
        const auto result = redactly::processVideo(
            *tools, samplePath, existingPath, *info, {}, {}, cancelled);
        assert(result.status == redactly::VideoProcessStatus::Failed);
        assert(result.error.contains("already exists"));
        assert(existing.open(QIODevice::ReadOnly));
        assert(existing.readAll() == "keep-existing-output");
        std::puts("existing output preservation: ok");
    }

    const QString bogusPath = tempDir.filePath("bogus.mp4");
    {
        QFile bogus(bogusPath);
        assert(bogus.open(QIODevice::WriteOnly));
        assert(bogus.write("this is not a video file") > 0);
    }
    QString corruptError;
    const auto corruptInfo = redactly::probeVideo(*tools, bogusPath, &corruptError);
    assert(!corruptInfo.has_value());
    assert(!corruptError.isEmpty());
    std::puts("corrupt input rejection: ok");

    const QString rotatedPath = tempDir.filePath("rotated.mp4");
    QProcess remux;
    remux.start(tools->ffmpegPath,
                {"-v", "error", "-y", "-display_rotation", "90",
                 "-i", samplePath, "-c", "copy", rotatedPath});
    remux.waitForStarted(15000);
    remux.waitForFinished(60000);
    if (remux.exitStatus() == QProcess::NormalExit && remux.exitCode() == 0)
    {
        const auto rotatedInfo = redactly::probeVideo(*tools, rotatedPath, &probeError);
        assert(rotatedInfo.has_value());
        assert(rotatedInfo->rotation == 90 || rotatedInfo->rotation == 270);
        assert(rotatedInfo->displayWidth() == 240);
        assert(rotatedInfo->displayHeight() == 320);

        redactly::VideoFrameReader rotatedReader;
        assert(rotatedReader.open(*tools, rotatedPath, *rotatedInfo));
        cv::Mat rotatedFrame;
        assert(rotatedReader.readFrame(rotatedFrame));
        assert(rotatedFrame.cols == 240);
        assert(rotatedFrame.rows == 320);
        rotatedReader.close();

        redactly::VideoFrameWriter rotatedWriter;
        assert(rotatedWriter.open(*tools, tempDir.filePath("rotated-out.mp4"),
                                  rotatedPath, *rotatedInfo,
                                  redactly::crfForQuality(redactly::VideoQuality::SpaceSaver)));
        assert(rotatedWriter.writeFrame(rotatedFrame));
        assert(rotatedWriter.finish());

        const auto rotatedOut = redactly::probeVideo(
            *tools, tempDir.filePath("rotated-out.mp4"), &probeError);
        assert(rotatedOut.has_value());
        assert(rotatedOut->rotation == 0);
        assert(rotatedOut->width == 240);
        assert(rotatedOut->height == 320);
        std::puts("rotation handling: ok");
    }
    else
    {
        std::puts("rotation handling: skipped (-display_rotation unsupported)");
    }

    {
        redactly::VideoFrameReader smallReader;
        assert(smallReader.open(*tools, samplePath, *info, 160));
        assert(smallReader.frameWidth() == 160);
        assert(smallReader.frameHeight() == 120);
        cv::Mat smallFrame;
        assert(smallReader.readFrame(smallFrame));
        assert(smallFrame.cols == 160 && smallFrame.rows == 120);
        std::puts("downscaled decode: ok");
    }

    {
        const QString processedPath = tempDir.filePath("processed.mp4");
        std::atomic<bool> cancelled{false};
        qint64 lastPass2Frame = 0;
        const auto result = redactly::processVideo(
            *tools, samplePath, processedPath, *info, {}, {}, cancelled,
            [&](int pass, qint64 frameIndex, qint64)
            {
                if (pass == 2)
                {
                    lastPass2Frame = frameIndex;
                }
            });
        assert(result.status == redactly::VideoProcessStatus::Completed);
        assert(result.trackCount == 0);
        assert(result.frameCount >= 55 && result.frameCount <= 65);
        assert(lastPass2Frame == result.frameCount);

        const auto processedInfo = redactly::probeVideo(*tools, processedPath, &probeError);
        assert(processedInfo.has_value());
        assert(processedInfo->videoCodec == "h264");
        assert(processedInfo->hasAudio);
        std::puts("video processor round trip: ok");
    }

    {
        redactly::VideoProcessOptions options;
        options.analysisLongEdge = 160;
        options.method = redactly::AnonymizationMethod::Fill;
        options.paddingRatio = 0.0F;
        const QString scaledPath = tempDir.filePath("scaled.mp4");
        std::atomic<bool> cancelled{false};
        bool reviewCalled = false;
        const auto result = redactly::processVideo(
            *tools, samplePath, scaledPath, *info, options,
            [](const cv::Mat &frame)
            {
                assert(frame.cols == 160 && frame.rows == 120);
                redactly::FaceDetections detections;
                detections.push_back({cv::Rect2f(40.0F, 30.0F, 80.0F, 60.0F), 0.9F});
                return detections;
            },
            cancelled, {},
            [&](std::vector<redactly::Track> &tracks, qint64 frameCount,
                const QString &, const redactly::VideoInfo &)
            {
                reviewCalled = true;
                assert(frameCount >= 55 && frameCount <= 65);
                assert(tracks.size() == 1);
                return true;
            });
        assert(result.status == redactly::VideoProcessStatus::Completed);
        assert(result.trackCount == 1);
        assert(reviewCalled);

        const auto scaledInfo = redactly::probeVideo(*tools, scaledPath, &probeError);
        assert(scaledInfo.has_value());
        redactly::VideoFrameReader outputReader;
        assert(outputReader.open(*tools, scaledPath, *scaledInfo));
        cv::Mat outputFrame;
        assert(outputReader.readFrame(outputFrame));
        const cv::Scalar inside = cv::mean(outputFrame(cv::Rect(85, 65, 150, 110)));
        const cv::Scalar outside = cv::mean(outputFrame(cv::Rect(0, 190, 70, 45)));
        assert(inside[0] + inside[1] + inside[2] < 30.0);
        assert(outside[0] + outside[1] + outside[2] > 45.0);
        std::puts("analysis-to-native coordinate scaling: ok");
    }

    {
        const QString reviewCancelledPath = tempDir.filePath("review-cancelled.mp4");
        std::atomic<bool> cancelled{false};
        bool reviewCalled = false;
        const auto result = redactly::processVideo(
            *tools, samplePath, reviewCancelledPath, *info, {},
            [](const cv::Mat &)
            {
                return redactly::FaceDetections{
                    {cv::Rect2f(40.0F, 30.0F, 80.0F, 60.0F), 0.9F}};
            },
            cancelled, {},
            [&](std::vector<redactly::Track> &, qint64, const QString &,
                const redactly::VideoInfo &)
            {
                reviewCalled = true;
                return false;
            });
        assert(reviewCalled);
        assert(result.status == redactly::VideoProcessStatus::Cancelled);
        assert(!QFile::exists(reviewCancelledPath));
        assert(stagingLeftovers(tempDir.path()) == 0);
        std::puts("video review cancellation: ok");
    }

    {
        const QString replaceableSource = tempDir.filePath("replaceable-source.mp4");
        const QString replacement = tempDir.filePath("replacement.mp4");
        const QString changedSourceOutput = tempDir.filePath("snapshotted-source.mp4");
        assert(QFile::copy(samplePath, replaceableSource));
        assert(generateSample(*tools, replacement, 1));
        const auto replaceableInfo = redactly::probeVideo(
            *tools, replaceableSource, &probeError);
        assert(replaceableInfo.has_value());

        std::atomic<bool> cancelled{false};
        bool reviewCalled = false;
        const auto result = redactly::processVideo(
            *tools, replaceableSource, changedSourceOutput, *replaceableInfo, {}, {},
            cancelled, {},
            [&](std::vector<redactly::Track> &, qint64, const QString &,
                const redactly::VideoInfo &)
            {
                reviewCalled = true;
                assert(stagingLeftovers(tempDir.path()) >= 1);
                assert(QFile::remove(replaceableSource));
                assert(QFile::rename(replacement, replaceableSource));
                return true;
            });
        assert(reviewCalled);
        assert(result.status == redactly::VideoProcessStatus::Completed);
        assert(result.frameCount >= 55 && result.frameCount <= 65);
        assert(QFile::exists(changedSourceOutput));
        assert(stagingLeftovers(tempDir.path()) == 0);
        std::puts("stable source snapshot between video passes: ok");
    }

    {
        std::atomic<bool> cancelled{true};
        const auto result = redactly::processVideo(
            *tools, samplePath, tempDir.filePath("cancelled.mp4"), *info, {},
            {}, cancelled);
        assert(result.status == redactly::VideoProcessStatus::Cancelled);
        assert(!QFile::exists(tempDir.filePath("cancelled.mp4")));
        std::puts("video processor cancellation: ok");
    }

    {
        const QString pass2CancelledPath = tempDir.filePath("pass2-cancelled.mp4");
        std::atomic<bool> cancelled{false};
        const auto result = redactly::processVideo(
            *tools, samplePath, pass2CancelledPath, *info, {}, {}, cancelled,
            [&](int pass, qint64 frame, qint64)
            {
                if (pass == 2 && frame == 1)
                {
                    cancelled.store(true, std::memory_order_release);
                }
            });
        assert(result.status == redactly::VideoProcessStatus::Cancelled);
        assert(!QFile::exists(pass2CancelledPath));
        std::puts("video pass-2 cancellation cleanup: ok");
    }

    std::puts("all video io tests passed");
    return 0;
}
