#pragma once

#include "faceveil/FaceDetection.hpp"

#include <opencv2/core.hpp>

namespace faceveil
{
    enum class AnonymizationMethod
    {
        Mosaic,
        Blur,
        Fill,
    };

    void applyAnonymization(cv::Mat &image, const FaceDetections &detections,
                            AnonymizationMethod method, int blockSize, float paddingRatio);

    void applyMosaic(cv::Mat &image, const FaceDetections &detections,
                     int blockSize, float paddingRatio);
}
