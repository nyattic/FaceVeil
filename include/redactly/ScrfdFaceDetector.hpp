#pragma once

#include "redactly/DetectionGeometry.hpp"
#include "redactly/Detector.hpp"
#include "redactly/FaceDetection.hpp"
#include "redactly/OrtAcceleration.hpp"

#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace redactly
{
    class ScrfdFaceDetector final : public Detector
    {
    public:
        explicit ScrfdFaceDetector(const std::string &modelPath, int inputSize = 640,
                                   bool enableAcceleration = false);

        FaceDetections detect(const cv::Mat &bgrImage, float scoreThreshold, float nmsThreshold) override;

        [[nodiscard]] OrtAccelerator accelerator() const noexcept { return accelerator_; }

        [[nodiscard]] int inputSize() const noexcept { return inputSize_; }

    private:
        struct PreparedImage
        {
            std::vector<float> tensor;
            int originalWidth = 0;
            int originalHeight = 0;
            float scale = 1.0F;
            int resizedWidth = 0;
            int resizedHeight = 0;
        };

        void adoptOriginalSessionIfAccelerated(const std::vector<std::uint8_t> &modelBytes);

        void adoptFixedInputSession(const std::vector<std::uint8_t> &modelBytes, int fallbackSize);

        [[nodiscard]] PreparedImage prepare(const cv::Mat &bgrImage) const;

        [[nodiscard]] FaceDetections decode(const std::vector<Ort::Value> &outputs,
                                            const PreparedImage &prepared,
                                            float scoreThreshold,
                                            float nmsThreshold) const;

        [[nodiscard]] static std::vector<cv::Point2f> anchorCenters(int featureHeight, int featureWidth, int stride);

        int inputSize_;
        OrtAccelerator accelerator_ = OrtAccelerator::None;
        Ort::Env env_;
        Ort::SessionOptions sessionOptions_;
        Ort::Session session_;
        std::vector<std::string> inputNames_;
        std::vector<std::string> outputNames_;
        std::vector<const char *> inputNamePtrs_;
        std::vector<const char *> outputNamePtrs_;
    };
}
