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
    std::puts("tracking tests passed");
    return 0;
}
