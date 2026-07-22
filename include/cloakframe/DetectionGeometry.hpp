#pragma once

#include "cloakframe/FaceDetection.hpp"

#include <opencv2/core.hpp>

#include <algorithm>
#include <vector>

namespace cloakframe
{
    inline float intersectionOverUnion(const cv::Rect2f &a, const cv::Rect2f &b)
    {
        const float areaA = a.area();
        const float areaB = b.area();
        if (areaA <= 0.0F || areaB <= 0.0F)
        {
            return 0.0F;
        }

        const float left = std::max(a.x, b.x);
        const float top = std::max(a.y, b.y);
        const float right = std::min(a.x + a.width, b.x + b.width);
        const float bottom = std::min(a.y + a.height, b.y + b.height);
        const float intersection = std::max(0.0F, right - left) * std::max(0.0F, bottom - top);
        return intersection / (areaA + areaB - intersection);
    }

    inline FaceDetections nonMaxSuppression(FaceDetections detections, float threshold)
    {
        std::erase_if(detections, [](const FaceDetection &detection)
        {
            return !isValidFaceDetection(detection);
        });
        std::ranges::sort(detections, [](const FaceDetection &a, const FaceDetection &b)
        {
            return a.score > b.score;
        });

        FaceDetections kept;
        std::vector<bool> suppressed(detections.size(), false);
        for (size_t i = 0; i < detections.size(); ++i)
        {
            if (suppressed[i])
            {
                continue;
            }
            kept.push_back(detections[i]);
            for (size_t j = i + 1; j < detections.size(); ++j)
            {
                if (!suppressed[j] && intersectionOverUnion(detections[i].box, detections[j].box) > threshold)
                {
                    suppressed[j] = true;
                }
            }
        }

        return kept;
    }
}
