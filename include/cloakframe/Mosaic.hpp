#pragma once

#include "cloakframe/FaceDetection.hpp"

#include <opencv2/core.hpp>

namespace cloakframe
{
    enum class AnonymizationMethod
    {
        Mosaic,
        Blur,
        Fill,
        Sticker,
    };

    enum class MaskShape
    {
        Rectangle,
        Ellipse,
    };

    void applyAnonymization(cv::Mat &image, const FaceDetections &detections,
                            AnonymizationMethod method, int blockSize, float paddingRatio,
                            MaskShape shape = MaskShape::Rectangle, bool softEdges = false);

    void applyMosaic(cv::Mat &image, const FaceDetections &detections,
                     int blockSize, float paddingRatio);
}
