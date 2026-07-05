#include "redactly/SceneCut.hpp"
#include "redactly/Tracking.hpp"

#include <opencv2/core.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    redactly::FaceDetection det(float x, float y, float score = 0.9F, float size = 40.0F)
    {
        return {cv::Rect2f(x, y, size, size), score};
    }

    std::vector<redactly::FaceDetections> movingObjectSequence(int frames, float startX, float stepX,
                                                               float y = 100.0F, float score = 0.9F)
    {
        std::vector<redactly::FaceDetections> sequence(frames);
        for (int frame = 0; frame < frames; ++frame)
        {
            sequence[frame].push_back(det(startX + stepX * static_cast<float>(frame), y, score));
        }
        return sequence;
    }

    bool contains(const cv::Rect2f &outer, const cv::Rect2f &inner)
    {
        constexpr float kEpsilon = 0.001F;
        return outer.x <= inner.x + kEpsilon
               && outer.y <= inner.y + kEpsilon
               && outer.x + outer.width >= inner.x + inner.width - kEpsilon
               && outer.y + outer.height >= inner.y + inner.height - kEpsilon;
    }

    void testConstantVelocitySingleTrack()
    {
        const auto tracks = redactly::buildTracks(movingObjectSequence(30, 50.0F, 5.0F));
        assert(tracks.size() == 1);
        assert(tracks[0].boxes.size() == 30);
        assert(tracks[0].firstFrame() == 0);
        assert(tracks[0].lastFrame() == 29);
        for (const auto &box: tracks[0].boxes)
        {
            assert(!box.interpolated);
        }
    }

    void testGapInterpolationFillsMissedFrames()
    {
        auto sequence = movingObjectSequence(20, 50.0F, 5.0F);
        sequence[10].clear();
        sequence[11].clear();
        sequence[12].clear();

        auto tracks = redactly::buildTracks(sequence);
        assert(tracks.size() == 1);
        assert(tracks[0].boxAtFrame(11) == nullptr);

        redactly::interpolateGaps(tracks[0], 30);
        assert(tracks[0].boxes.size() == 20);

        const auto *filled = tracks[0].boxAtFrame(11);
        assert(filled != nullptr);
        assert(filled->interpolated);
        const float expectedX = 50.0F + 5.0F * 11.0F;
        assert(std::abs(filled->box.x - expectedX) < 1.0F);
    }

    void testGapLargerThanLimitIsNotInterpolated()
    {
        auto sequence = movingObjectSequence(20, 50.0F, 5.0F);
        for (int frame = 5; frame <= 12; ++frame)
        {
            sequence[frame].clear();
        }

        auto tracks = redactly::buildTracks(sequence);
        assert(tracks.size() == 1);
        redactly::interpolateGaps(tracks[0], 3);
        assert(tracks[0].boxAtFrame(8) == nullptr);
    }

    void testLowConfidenceDetectionsExtendButNeverStartTracks()
    {
        auto sequence = movingObjectSequence(15, 50.0F, 5.0F);
        for (int frame = 5; frame <= 7; ++frame)
        {
            sequence[frame][0].score = 0.2F;
        }
        const auto tracks = redactly::buildTracks(sequence);
        assert(tracks.size() == 1);
        assert(tracks[0].boxes.size() == 15);
        assert(tracks[0].boxAtFrame(6) != nullptr);

        const auto lowOnly = redactly::buildTracks(movingObjectSequence(15, 50.0F, 5.0F, 100.0F, 0.2F));
        assert(lowOnly.empty());
    }

    void testCrossingObjectsKeepTwoTracks()
    {
        std::vector<redactly::FaceDetections> sequence(21);
        for (int frame = 0; frame <= 20; ++frame)
        {
            const auto offset = 10.0F * static_cast<float>(frame);
            sequence[frame].push_back(det(0.0F + offset, 100.0F));
            sequence[frame].push_back(det(200.0F - offset, 100.0F));
        }

        const auto tracks = redactly::buildTracks(sequence);
        assert(tracks.size() == 2);
        for (int frame = 0; frame <= 20; ++frame)
        {
            assert(redactly::trackRegionsForFrame(tracks, frame).size() == 2);
        }
    }

    void testBidirectionalMergeProducesSingleTrack()
    {
        const auto tracks = redactly::buildBidirectionalTracks(movingObjectSequence(20, 50.0F, 5.0F));
        assert(tracks.size() == 1);
        assert(tracks[0].boxes.size() == 20);
    }

    void testBackwardPassRecoversLowConfidenceStart()
    {
        auto sequence = movingObjectSequence(20, 50.0F, 5.0F);
        for (int frame = 0; frame <= 4; ++frame)
        {
            sequence[frame][0].score = 0.2F;
        }

        const auto forwardOnly = redactly::buildTracks(sequence);
        assert(forwardOnly.size() == 1);
        assert(forwardOnly[0].firstFrame() == 5);

        const auto merged = redactly::buildBidirectionalTracks(sequence);
        assert(merged.size() == 1);
        assert(merged[0].firstFrame() == 0);
        assert(merged[0].boxes.size() == 20);
    }

    void testSmoothingNeverUncoversDetections()
    {
        std::vector<redactly::FaceDetections> sequence(20);
        for (int frame = 0; frame < 20; ++frame)
        {
            const float jitter = (frame % 2 == 0) ? 3.0F : -3.0F;
            sequence[frame].push_back(det(100.0F + 5.0F * static_cast<float>(frame) + jitter,
                                          100.0F + jitter));
        }

        auto tracks = redactly::buildTracks(sequence);
        assert(tracks.size() == 1);
        const auto original = tracks[0];

        redactly::postProcessTracks(tracks, {}, 20);
        for (const auto &box: original.boxes)
        {
            const auto *processed = tracks[0].boxAtFrame(box.frame);
            assert(processed != nullptr);
            assert(contains(processed->box, box.box));
        }
    }

    void testSmoothingDoesNotInflateMovingBoxes()
    {
        const auto sequence = movingObjectSequence(30, 0.0F, 12.0F);
        auto tracks = redactly::buildBidirectionalTracks(sequence);
        redactly::postProcessTracks(tracks, {}, 30);
        assert(tracks.size() == 1);

        for (int frame = 0; frame < 30; ++frame)
        {
            const auto *processed = tracks[0].boxAtFrame(frame);
            assert(processed != nullptr);
            if (processed->interpolated)
            {
                continue;
            }
            const auto &detected = sequence[frame][0].box;
            assert(contains(processed->box, detected));
            assert(processed->box.area() <= detected.area() * 1.26F);
        }
    }

    void testExtendTrackEndsClampsToVideoBounds()
    {
        auto sequence = movingObjectSequence(12, 50.0F, 5.0F);
        for (int frame = 0; frame < 5; ++frame)
        {
            sequence[frame].clear();
        }
        for (int frame = 11; frame < 12; ++frame)
        {
            sequence[frame].clear();
        }

        auto tracks = redactly::buildTracks(sequence);
        assert(tracks.size() == 1);
        assert(tracks[0].firstFrame() == 5);
        assert(tracks[0].lastFrame() == 10);

        redactly::extendTrackEnds(tracks[0], 100, 12);
        assert(tracks[0].firstFrame() == 0);
        assert(tracks[0].lastFrame() == 11);
        assert(tracks[0].boxes.size() == 12);
        assert(tracks[0].boxAtFrame(2)->interpolated);
    }

    void testRegionsForFrameCollectsAllTracks()
    {
        std::vector<redactly::FaceDetections> sequence(10);
        for (int frame = 0; frame < 10; ++frame)
        {
            sequence[frame].push_back(det(50.0F, 50.0F));
            sequence[frame].push_back(det(300.0F, 200.0F));
        }
        const auto tracks = redactly::buildTracks(sequence);
        assert(tracks.size() == 2);
        assert(redactly::trackRegionsForFrame(tracks, 5).size() == 2);
        assert(redactly::trackRegionsForFrame(tracks, 42).empty());
    }

    void testSceneCutsContainer()
    {
        const redactly::SceneCuts cuts({15, 5, 5, 0, -3});
        assert(cuts.frames() == std::vector<int>({5, 15}));
        assert(cuts.isCut(5));
        assert(!cuts.isCut(6));
        assert(cuts.spansCut(4, 5));
        assert(cuts.spansCut(0, 19));
        assert(cuts.spansCut(14, 16));
        assert(!cuts.spansCut(5, 14));
        assert(!cuts.spansCut(15, 19));
        assert(!redactly::SceneCuts{}.spansCut(0, 1000));

        const auto reversed = cuts.reversed(20);
        assert(reversed.frames() == std::vector<int>({5, 15}));
        const auto asymmetric = redactly::SceneCuts({7}).reversed(20);
        assert(asymmetric.frames() == std::vector<int>({13}));
    }

    void testTrackerSplitsTracksAtSceneCut()
    {
        const auto sequence = movingObjectSequence(20, 50.0F, 0.0F);

        const auto unbroken = redactly::buildTracks(sequence);
        assert(unbroken.size() == 1);

        const auto split = redactly::buildTracks(sequence, {}, redactly::SceneCuts({10}));
        assert(split.size() == 2);
        for (const auto &track: split)
        {
            assert(track.firstFrame() >= 10 || track.lastFrame() < 10);
        }
    }

    void testNoInterpolationAcrossSceneCut()
    {
        redactly::Track track;
        track.id = 1;
        track.boxes.push_back({5, cv::Rect2f(50.0F, 50.0F, 40.0F, 40.0F), 0.9F, false});
        track.boxes.push_back({15, cv::Rect2f(50.0F, 50.0F, 40.0F, 40.0F), 0.9F, false});

        auto gated = track;
        redactly::interpolateGaps(gated, 30, redactly::SceneCuts({10}));
        assert(gated.boxes.size() == 2);

        redactly::interpolateGaps(track, 30);
        assert(track.boxes.size() == 11);
    }

    void testExtendTrackEndsStopsAtSceneCut()
    {
        redactly::Track track;
        track.id = 1;
        for (int frame = 10; frame <= 15; ++frame)
        {
            track.boxes.push_back({frame, cv::Rect2f(50.0F, 50.0F, 40.0F, 40.0F), 0.9F, false});
        }

        redactly::extendTrackEnds(track, 5, 30, redactly::SceneCuts({10, 16}));
        assert(track.firstFrame() == 10);
        assert(track.lastFrame() == 15);
    }

    void testBidirectionalTracksRespectSceneCuts()
    {
        const auto sequence = movingObjectSequence(20, 50.0F, 5.0F);
        const auto tracks =
                redactly::buildBidirectionalTracks(sequence, {}, 0.5F, redactly::SceneCuts({10}));
        assert(tracks.size() == 2);
        for (const auto &track: tracks)
        {
            assert(track.firstFrame() >= 10 || track.lastFrame() < 10);
        }
        for (int frame = 0; frame < 20; ++frame)
        {
            assert(redactly::trackRegionsForFrame(tracks, frame).size() == 1);
        }
    }

    cv::Mat gradientFrame(bool horizontal, int width = 480, int height = 270)
    {
        cv::Mat frame(height, width, CV_8UC1);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const int value = horizontal ? x * 255 / (width - 1) : y * 255 / (height - 1);
                frame.at<unsigned char>(y, x) = static_cast<unsigned char>(value);
            }
        }
        return frame;
    }

    void testSceneCutDetectorFindsHardCut()
    {
        const auto sceneA = gradientFrame(true);
        const auto sceneB = gradientFrame(false);

        redactly::SceneCutDetector detector;
        for (int frame = 0; frame < 10; ++frame)
        {
            detector.push(sceneA);
        }
        for (int frame = 0; frame < 5; ++frame)
        {
            detector.push(sceneB);
        }
        const auto cuts = detector.finish();
        assert(cuts.frames() == std::vector<int>({10}));
    }

    void testSceneCutDetectorIgnoresFlash()
    {
        const auto scene = gradientFrame(true);
        const cv::Mat flash(270, 480, CV_8UC1, cv::Scalar(255));

        redactly::SceneCutDetector detector;
        for (int frame = 0; frame < 10; ++frame)
        {
            detector.push(scene);
        }
        detector.push(flash);
        for (int frame = 0; frame < 10; ++frame)
        {
            detector.push(scene);
        }
        assert(detector.finish().empty());
    }

    void testSceneCutDetectorIgnoresStaticScene()
    {
        const auto scene = gradientFrame(true);
        redactly::SceneCutDetector detector;
        for (int frame = 0; frame < 30; ++frame)
        {
            detector.push(scene);
        }
        assert(detector.finish().empty());
    }

    void testSceneCutDetectorCommitsTrailingCandidate()
    {
        const auto sceneA = gradientFrame(true);
        const auto sceneB = gradientFrame(false);

        redactly::SceneCutDetector detector;
        for (int frame = 0; frame < 5; ++frame)
        {
            detector.push(sceneA);
        }
        detector.push(sceneB);
        const auto cuts = detector.finish();
        assert(cuts.frames() == std::vector<int>({5}));
    }
}

int main()
{
    testConstantVelocitySingleTrack();
    testGapInterpolationFillsMissedFrames();
    testGapLargerThanLimitIsNotInterpolated();
    testLowConfidenceDetectionsExtendButNeverStartTracks();
    testCrossingObjectsKeepTwoTracks();
    testBidirectionalMergeProducesSingleTrack();
    testBackwardPassRecoversLowConfidenceStart();
    testSmoothingNeverUncoversDetections();
    testSmoothingDoesNotInflateMovingBoxes();
    testExtendTrackEndsClampsToVideoBounds();
    testRegionsForFrameCollectsAllTracks();
    testSceneCutsContainer();
    testTrackerSplitsTracksAtSceneCut();
    testNoInterpolationAcrossSceneCut();
    testExtendTrackEndsStopsAtSceneCut();
    testBidirectionalTracksRespectSceneCuts();
    testSceneCutDetectorFindsHardCut();
    testSceneCutDetectorIgnoresFlash();
    testSceneCutDetectorIgnoresStaticScene();
    testSceneCutDetectorCommitsTrailingCandidate();
    std::puts("tracking tests passed");
    return 0;
}
