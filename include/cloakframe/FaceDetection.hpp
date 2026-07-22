#pragma once

#include <opencv2/core.hpp>

#include <cmath>
#include <vector>

namespace cloakframe
{
    struct FaceDetection
    {
        cv::Rect2f box;
        float score = 0.0F;
    };

    inline bool isValidFaceDetection(const FaceDetection &detection)
    {
        return std::isfinite(detection.score) &&
               std::isfinite(detection.box.x) &&
               std::isfinite(detection.box.y) &&
               std::isfinite(detection.box.width) &&
               std::isfinite(detection.box.height) &&
               detection.box.width > 0.0F && detection.box.height > 0.0F;
    }

    using FaceDetections = std::vector<FaceDetection>;
}
