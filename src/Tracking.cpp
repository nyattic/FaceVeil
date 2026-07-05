#include "redactly/Tracking.hpp"

#include "redactly/DetectionGeometry.hpp"

#include <algorithm>
#include <utility>

namespace redactly
{
    namespace
    {
        cv::Point2f boxCenter(const cv::Rect2f &box)
        {
            return {box.x + box.width * 0.5F, box.y + box.height * 0.5F};
        }

        cv::Rect2f boxWithCenter(const cv::Rect2f &box, const cv::Point2f &center)
        {
            return {center.x - box.width * 0.5F, center.y - box.height * 0.5F, box.width, box.height};
        }

        cv::Rect2f boxUnion(const cv::Rect2f &a, const cv::Rect2f &b)
        {
            const float left = std::min(a.x, b.x);
            const float top = std::min(a.y, b.y);
            const float right = std::max(a.x + a.width, b.x + b.width);
            const float bottom = std::max(a.y + a.height, b.y + b.height);
            return {left, top, right - left, bottom - top};
        }

        cv::Rect2f lerpBox(const cv::Rect2f &a, const cv::Rect2f &b, float t)
        {
            return {a.x + (b.x - a.x) * t,
                    a.y + (b.y - a.y) * t,
                    a.width + (b.width - a.width) * t,
                    a.height + (b.height - a.height) * t};
        }

        struct Match
        {
            size_t trackIndex;
            size_t detectionIndex;
            float iou;
        };

        std::vector<Match> greedyIouMatches(const std::vector<cv::Rect2f> &predictions,
                                            const std::vector<size_t> &trackIndices,
                                            const FaceDetections &detections,
                                            const std::vector<size_t> &detectionIndices,
                                            float iouThreshold)
        {
            std::vector<Match> candidates;
            for (const size_t trackIndex: trackIndices)
            {
                for (const size_t detectionIndex: detectionIndices)
                {
                    const float iou = intersectionOverUnion(predictions[trackIndex],
                                                            detections[detectionIndex].box);
                    if (iou >= iouThreshold)
                    {
                        candidates.push_back({trackIndex, detectionIndex, iou});
                    }
                }
            }

            std::ranges::sort(candidates, [](const Match &a, const Match &b)
            {
                return a.iou > b.iou;
            });

            std::vector<Match> matches;
            std::vector<bool> trackUsed(predictions.size(), false);
            std::vector<bool> detectionUsed(detections.size(), false);
            for (const auto &candidate: candidates)
            {
                if (trackUsed[candidate.trackIndex] || detectionUsed[candidate.detectionIndex])
                {
                    continue;
                }
                trackUsed[candidate.trackIndex] = true;
                detectionUsed[candidate.detectionIndex] = true;
                matches.push_back(candidate);
            }
            return matches;
        }
    }

    int Track::firstFrame() const
    {
        return boxes.empty() ? 0 : boxes.front().frame;
    }

    int Track::lastFrame() const
    {
        return boxes.empty() ? 0 : boxes.back().frame;
    }

    const TrackedBox *Track::boxAtFrame(int frame) const
    {
        const auto it = std::ranges::lower_bound(boxes, frame, {}, &TrackedBox::frame);
        if (it != boxes.end() && it->frame == frame)
        {
            return &*it;
        }
        return nullptr;
    }

    cv::Rect2f ByteTracker::ActiveTrack::predictedBox(int frame) const
    {
        const auto &last = track.boxes.back();
        const float dt = static_cast<float>(frame - lastFrame);
        const cv::Point2f center = boxCenter(last.box) + velocity * dt;
        return boxWithCenter(last.box, center);
    }

    ByteTracker::ByteTracker(TrackerConfig config, SceneCuts cuts)
        : config_(config)
        , cuts_(std::move(cuts))
    {
    }

    void ByteTracker::extendTrack(ActiveTrack &active, int frame, const FaceDetection &detection)
    {
        const auto &last = active.track.boxes.back();
        const float dt = static_cast<float>(frame - active.lastFrame);
        if (dt > 0.0F)
        {
            const cv::Point2f observed = (boxCenter(detection.box) - boxCenter(last.box)) / dt;
            active.velocity = active.velocity * (1.0F - config_.velocityBlend)
                              + observed * config_.velocityBlend;
        }
        active.track.boxes.push_back({frame, detection.box, detection.score, false});
        active.lastFrame = frame;
    }

    void ByteTracker::update(int frame, const FaceDetections &detections)
    {
        if (cuts_.isCut(frame))
        {
            for (auto &active: active_)
            {
                finished_.push_back(std::move(active.track));
            }
            active_.clear();
        }

        std::erase_if(active_, [&](ActiveTrack &active)
        {
            if (frame - active.lastFrame > config_.maxFramesLost)
            {
                finished_.push_back(std::move(active.track));
                return true;
            }
            return false;
        });

        std::vector<cv::Rect2f> predictions(active_.size());
        std::vector<size_t> unmatchedTracks(active_.size());
        for (size_t i = 0; i < active_.size(); ++i)
        {
            predictions[i] = active_[i].predictedBox(frame);
            unmatchedTracks[i] = i;
        }

        std::vector<size_t> highDetections;
        std::vector<size_t> lowDetections;
        for (size_t i = 0; i < detections.size(); ++i)
        {
            if (detections[i].score >= config_.highScoreThreshold)
            {
                highDetections.push_back(i);
            }
            else if (detections[i].score >= config_.lowScoreThreshold)
            {
                lowDetections.push_back(i);
            }
        }

        const auto removeMatched = [](std::vector<size_t> &indices, size_t value)
        {
            std::erase(indices, value);
        };

        const auto highMatches = greedyIouMatches(predictions, unmatchedTracks, detections,
                                                  highDetections, config_.iouThreshold);
        for (const auto &match: highMatches)
        {
            extendTrack(active_[match.trackIndex], frame, detections[match.detectionIndex]);
            removeMatched(unmatchedTracks, match.trackIndex);
            removeMatched(highDetections, match.detectionIndex);
        }

        const auto lowMatches = greedyIouMatches(predictions, unmatchedTracks, detections,
                                                 lowDetections, config_.iouThreshold);
        for (const auto &match: lowMatches)
        {
            extendTrack(active_[match.trackIndex], frame, detections[match.detectionIndex]);
            removeMatched(unmatchedTracks, match.trackIndex);
        }

        for (const size_t detectionIndex: highDetections)
        {
            ActiveTrack fresh;
            fresh.track.id = nextId_++;
            fresh.track.boxes.push_back({frame, detections[detectionIndex].box,
                                         detections[detectionIndex].score, false});
            fresh.lastFrame = frame;
            active_.push_back(std::move(fresh));
        }
    }

    std::vector<Track> ByteTracker::finish()
    {
        for (auto &active: active_)
        {
            finished_.push_back(std::move(active.track));
        }
        active_.clear();

        std::vector<Track> result = std::move(finished_);
        finished_ = {};
        std::erase_if(result, [](const Track &track)
        {
            return track.boxes.empty();
        });
        std::ranges::sort(result, {}, &Track::id);
        return result;
    }

    std::vector<Track> buildTracks(const std::vector<FaceDetections> &frameDetections,
                                   const TrackerConfig &config,
                                   const SceneCuts &cuts)
    {
        ByteTracker tracker(config, cuts);
        for (size_t frame = 0; frame < frameDetections.size(); ++frame)
        {
            tracker.update(static_cast<int>(frame), frameDetections[frame]);
        }
        return tracker.finish();
    }

    std::vector<Track> buildBidirectionalTracks(const std::vector<FaceDetections> &frameDetections,
                                                const TrackerConfig &config,
                                                float mergeIouThreshold,
                                                const SceneCuts &cuts)
    {
        auto forward = buildTracks(frameDetections, config, cuts);

        const int frameCount = static_cast<int>(frameDetections.size());
        std::vector<FaceDetections> reversed(frameDetections.rbegin(), frameDetections.rend());
        auto backward = buildTracks(reversed, config, cuts.reversed(frameCount));
        for (auto &track: backward)
        {
            for (auto &box: track.boxes)
            {
                box.frame = frameCount - 1 - box.frame;
            }
            std::ranges::reverse(track.boxes);
        }

        int nextId = 1;
        for (const auto &track: forward)
        {
            nextId = std::max(nextId, track.id + 1);
        }

        for (auto &candidate: backward)
        {
            Track *bestMatch = nullptr;
            float bestScore = 0.0F;
            for (auto &existing: forward)
            {
                int sharedFrames = 0;
                float iouSum = 0.0F;
                for (const auto &box: candidate.boxes)
                {
                    if (const auto *other = existing.boxAtFrame(box.frame))
                    {
                        ++sharedFrames;
                        iouSum += intersectionOverUnion(box.box, other->box);
                    }
                }
                if (sharedFrames == 0)
                {
                    continue;
                }
                const float meanIou = iouSum / static_cast<float>(sharedFrames);
                if (meanIou >= mergeIouThreshold && meanIou > bestScore)
                {
                    bestScore = meanIou;
                    bestMatch = &existing;
                }
            }

            if (bestMatch == nullptr)
            {
                candidate.id = nextId++;
                forward.push_back(std::move(candidate));
                continue;
            }

            std::vector<TrackedBox> missing;
            for (const auto &box: candidate.boxes)
            {
                if (bestMatch->boxAtFrame(box.frame) == nullptr)
                {
                    missing.push_back(box);
                }
            }
            bestMatch->boxes.insert(bestMatch->boxes.end(), missing.begin(), missing.end());
            std::ranges::sort(bestMatch->boxes, {}, &TrackedBox::frame);
        }

        return forward;
    }

    void interpolateGaps(Track &track, int maxGap, const SceneCuts &cuts)
    {
        if (track.boxes.size() < 2 || maxGap < 1)
        {
            return;
        }

        std::vector<TrackedBox> filled;
        filled.reserve(track.boxes.size());
        for (size_t i = 0; i + 1 < track.boxes.size(); ++i)
        {
            const auto &current = track.boxes[i];
            const auto &next = track.boxes[i + 1];
            filled.push_back(current);

            const int gap = next.frame - current.frame - 1;
            if (gap < 1 || gap > maxGap || cuts.spansCut(current.frame, next.frame))
            {
                continue;
            }
            for (int step = 1; step <= gap; ++step)
            {
                const float t = static_cast<float>(step) / static_cast<float>(gap + 1);
                filled.push_back({current.frame + step,
                                  lerpBox(current.box, next.box, t),
                                  std::min(current.score, next.score),
                                  true});
            }
        }
        filled.push_back(track.boxes.back());
        track.boxes = std::move(filled);
    }

    void smoothTrack(Track &track, int radius)
    {
        if (radius < 1 || track.boxes.size() < 3)
        {
            return;
        }

        const auto original = track.boxes;
        for (size_t i = 0; i < track.boxes.size(); ++i)
        {
            const size_t begin = i >= static_cast<size_t>(radius) ? i - static_cast<size_t>(radius) : 0;
            const size_t end = std::min(track.boxes.size() - 1, i + static_cast<size_t>(radius));

            cv::Rect2f accumulated{0.0F, 0.0F, 0.0F, 0.0F};
            int count = 0;
            for (size_t j = begin; j <= end; ++j)
            {
                const int frameDelta = original[j].frame - original[i].frame;
                const int indexDelta = static_cast<int>(j) - static_cast<int>(i);
                if (frameDelta != indexDelta)
                {
                    continue;
                }
                accumulated.x += original[j].box.x;
                accumulated.y += original[j].box.y;
                accumulated.width += original[j].box.width;
                accumulated.height += original[j].box.height;
                ++count;
            }
            if (count < 2)
            {
                continue;
            }
            const float inverse = 1.0F / static_cast<float>(count);
            const cv::Rect2f smoothed{accumulated.x * inverse, accumulated.y * inverse,
                                      accumulated.width * inverse, accumulated.height * inverse};
            if (original[i].interpolated)
            {
                track.boxes[i].box = smoothed;
                continue;
            }
            constexpr float kMaxSmoothingAreaGrowth = 1.25F;
            const cv::Rect2f covered = boxUnion(smoothed, original[i].box);
            const float originalArea = original[i].box.area();
            track.boxes[i].box =
                    (originalArea > 0.0F &&
                     covered.area() > originalArea * kMaxSmoothingAreaGrowth)
                        ? original[i].box
                        : covered;
        }
    }

    void extendTrackEnds(Track &track, int frames, int frameCount, const SceneCuts &cuts)
    {
        if (track.boxes.empty() || frames < 1 || frameCount < 1)
        {
            return;
        }

        std::vector<TrackedBox> prefix;
        const auto &first = track.boxes.front();
        for (int frame = std::max(0, first.frame - frames); frame < first.frame; ++frame)
        {
            if (cuts.spansCut(frame, first.frame))
            {
                continue;
            }
            prefix.push_back({frame, first.box, first.score, true});
        }

        std::vector<TrackedBox> suffix;
        const auto &last = track.boxes.back();
        const int lastAllowed = frameCount - 1;
        for (int frame = last.frame + 1; frame <= std::min(lastAllowed, last.frame + frames); ++frame)
        {
            if (cuts.spansCut(last.frame, frame))
            {
                break;
            }
            suffix.push_back({frame, last.box, last.score, true});
        }

        track.boxes.insert(track.boxes.begin(), prefix.begin(), prefix.end());
        track.boxes.insert(track.boxes.end(), suffix.begin(), suffix.end());
    }

    void postProcessTracks(std::vector<Track> &tracks, const TrackPostProcessConfig &config,
                           int frameCount, const SceneCuts &cuts)
    {
        for (auto &track: tracks)
        {
            interpolateGaps(track, config.maxInterpolationGap, cuts);
            smoothTrack(track, config.smoothingRadius);
            extendTrackEnds(track, config.extensionFrames, frameCount, cuts);
        }
    }

    std::vector<cv::Rect2f> trackRegionsForFrame(const std::vector<Track> &tracks, int frame)
    {
        std::vector<cv::Rect2f> regions;
        for (const auto &track: tracks)
        {
            if (const auto *box = track.boxAtFrame(frame))
            {
                regions.push_back(box->box);
            }
        }
        return regions;
    }
}
