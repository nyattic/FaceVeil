#pragma once

#include "redactly/Detector.hpp"
#include "redactly/OrtAcceleration.hpp"

#include <onnxruntime_cxx_api.h>

#include <string>
#include <vector>

namespace redactly
{
    class YunetFaceDetector final : public Detector
    {
    public:
        explicit YunetFaceDetector(const std::string &modelPath, int inputSize = 640,
                                   bool enableAcceleration = true);

        FaceDetections detect(const cv::Mat &bgrImage, float scoreThreshold,
                              float nmsThreshold) override;

        [[nodiscard]] int inputSize() const noexcept { return inputSize_; }
        [[nodiscard]] OrtAccelerator accelerator() const noexcept { return accelerator_; }

    private:
        struct PreparedImage
        {
            std::vector<float> tensor;
            float scale = 1.0F;
            int originalWidth = 0;
            int originalHeight = 0;
        };

        void adoptOriginalSessionIfAccelerated(const std::vector<std::uint8_t> &modelBytes);
        void adoptFixedInputSession(const std::vector<std::uint8_t> &modelBytes, int fallbackSize);

        [[nodiscard]] PreparedImage prepare(const cv::Mat &bgrImage) const;
        [[nodiscard]] FaceDetections decode(const std::vector<Ort::Value> &outputs,
                                            const PreparedImage &prepared,
                                            float scoreThreshold,
                                            float nmsThreshold) const;

        int inputSize_;
        Ort::Env env_;
        Ort::SessionOptions sessionOptions_;
        Ort::Session session_;
        OrtAccelerator accelerator_ = OrtAccelerator::None;
        std::vector<std::string> inputNames_;
        std::vector<std::string> outputNames_;
        std::vector<const char *> inputNamePtrs_;
        std::vector<const char *> outputNamePtrs_;
    };
}
