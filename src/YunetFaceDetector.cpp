#include "redactly/YunetFaceDetector.hpp"

#include "redactly/DetectionGeometry.hpp"
#include "redactly/OnnxGraphPatch.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace redactly
{
    namespace
    {
        constexpr std::array<int, 3> kStrides = {8, 16, 32};
        constexpr int kChannels = 3;
        constexpr int kMaxInputSize = 2048;
        constexpr size_t kMaxCandidatesBeforeNms = 5000;
        constexpr std::uintmax_t kMaxModelFileBytes = 512ULL << 20;

        std::vector<std::uint8_t> readModelFile(const std::filesystem::path &path)
        {
            std::error_code sizeError;
            const auto size = std::filesystem::file_size(path, sizeError);
            if (sizeError || size == 0 || size > kMaxModelFileBytes)
            {
                throw std::runtime_error("Could not read the model file.");
            }
            std::ifstream stream(path, std::ios::binary);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            if (!stream.read(reinterpret_cast<char *>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size())))
            {
                throw std::runtime_error("Could not read the model file.");
            }
            return bytes;
        }

        const Ort::Value *outputNamed(const std::vector<Ort::Value> &outputs,
                                      const std::vector<std::string> &names,
                                      std::string_view wanted)
        {
            const auto it = std::ranges::find(names, wanted);
            if (it == names.end())
            {
                return nullptr;
            }
            const auto index = static_cast<std::size_t>(std::distance(names.begin(), it));
            return index < outputs.size() ? &outputs[index] : nullptr;
        }

        const float *tensorData(const Ort::Value *value, std::size_t expectedElements,
                                int expectedLastDimension)
        {
            if (value == nullptr || !value->IsTensor())
            {
                return nullptr;
            }
            const auto info = value->GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
                info.GetElementCount() != expectedElements)
            {
                return nullptr;
            }
            const auto shape = info.GetShape();
            if (shape.empty() || shape.back() != expectedLastDimension)
            {
                return nullptr;
            }
            return value->GetTensorData<float>();
        }
    }

    YunetFaceDetector::YunetFaceDetector(const std::string &modelPath, int inputSize,
                                         bool enableAcceleration)
        : inputSize_(inputSize),
          env_(ORT_LOGGING_LEVEL_WARNING, "Redactly"),
          sessionOptions_(),
          session_(nullptr)
    {
        if (inputSize_ <= 0 || inputSize_ > kMaxInputSize || inputSize_ % kStrides.back() != 0)
        {
            throw std::runtime_error("YuNet input size must be a positive multiple of 32.");
        }

        sessionOptions_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        accelerator_ = applyOrtAcceleration(sessionOptions_, enableAcceleration);
        sessionOptions_.SetIntraOpNumThreads(
            accelerator_ == OrtAccelerator::None
                ? static_cast<int>(std::max(1U, std::thread::hardware_concurrency()))
                : 1);

        const std::u8string modelU8(modelPath.begin(), modelPath.end());
        const auto modelBytes = readModelFile(std::filesystem::path(modelU8));
        if (accelerator_ == OrtAccelerator::None)
        {
            session_ = Ort::Session(env_, modelBytes.data(), modelBytes.size(), sessionOptions_);
        }
        else
        {
            Ort::SessionOptions metadataOptions;
            metadataOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
            metadataOptions.SetIntraOpNumThreads(1);
            session_ = Ort::Session(env_, modelBytes.data(), modelBytes.size(), metadataOptions);
        }

        Ort::AllocatorWithDefaultOptions allocator;
        const auto inputCount = session_.GetInputCount();
        for (size_t i = 0; i < inputCount; ++i)
        {
            auto name = session_.GetInputNameAllocated(i, allocator);
            inputNames_.emplace_back(name.get());
        }
        const auto outputCount = session_.GetOutputCount();
        for (size_t i = 0; i < outputCount; ++i)
        {
            auto name = session_.GetOutputNameAllocated(i, allocator);
            outputNames_.emplace_back(name.get());
        }

        if (inputNames_.size() != 1 || outputNames_.size() != kStrides.size() * 4U)
        {
            throw std::runtime_error("The selected model does not look like a YuNet ONNX model.");
        }
        for (const int stride: kStrides)
        {
            for (const std::string_view prefix: {"cls_", "obj_", "bbox_", "kps_"})
            {
                const std::string name = std::string(prefix) + std::to_string(stride);
                if (std::ranges::find(outputNames_, name) == outputNames_.end())
                {
                    throw std::runtime_error("The selected model is missing a YuNet output tensor.");
                }
            }
        }

        const auto inputTypeInfo = session_.GetInputTypeInfo(0);
        const auto inputInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
        if (inputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
        {
            throw std::runtime_error(
                "YuNet model input must be a float tensor (element type " +
                std::to_string(static_cast<int>(inputInfo.GetElementType())) + ").");
        }
        const auto inputShape = inputInfo.GetShape();
        if (inputShape.size() != 4 || (inputShape[0] > 0 && inputShape[0] != 1) ||
            (inputShape[1] > 0 && inputShape[1] != kChannels))
        {
            throw std::runtime_error("YuNet model input must be a [1, 3, H, W] tensor.");
        }
        const int64_t modelHeight = inputShape[2];
        const int64_t modelWidth = inputShape[3];
        const bool fixed = modelHeight > 0 && modelWidth > 0;
        if ((modelHeight > 0) != (modelWidth > 0) || (fixed && modelHeight != modelWidth))
        {
            throw std::runtime_error("YuNet model spatial input must be square or dynamic.");
        }
        if (fixed && static_cast<int>(modelHeight) == inputSize_)
        {
            adoptOriginalSessionIfAccelerated(modelBytes);
        }
        else
        {
            adoptFixedInputSession(modelBytes, fixed ? static_cast<int>(modelHeight) : 0);
        }

        inputNamePtrs_.reserve(inputNames_.size());
        outputNamePtrs_.reserve(outputNames_.size());
        for (const auto &name: inputNames_)
        {
            inputNamePtrs_.push_back(name.c_str());
        }
        for (const auto &name: outputNames_)
        {
            outputNamePtrs_.push_back(name.c_str());
        }
    }

    void YunetFaceDetector::adoptOriginalSessionIfAccelerated(
            const std::vector<std::uint8_t> &modelBytes)
    {
        if (accelerator_ != OrtAccelerator::None)
        {
            session_ = Ort::Session(env_, modelBytes.data(), modelBytes.size(), sessionOptions_);
        }
    }

    void YunetFaceDetector::adoptFixedInputSession(const std::vector<std::uint8_t> &modelBytes,
                                                   int fallbackSize)
    {
        const auto patched = makeOnnxSpatialDimsFixed(modelBytes, inputSize_);
        if (patched)
        {
            try
            {
                Ort::Session fixedSession(env_, patched->data(), patched->size(), sessionOptions_);
                std::vector<float> probe(
                    static_cast<std::size_t>(kChannels) * inputSize_ * inputSize_, 0.0F);
                const std::array<int64_t, 4> shape = {1, kChannels, inputSize_, inputSize_};
                auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                auto tensor = Ort::Value::CreateTensor<float>(
                    memory, probe.data(), probe.size(), shape.data(), shape.size());
                std::vector<const char *> inputs;
                std::vector<const char *> outputs;
                for (const auto &name: inputNames_) inputs.push_back(name.c_str());
                for (const auto &name: outputNames_) outputs.push_back(name.c_str());
                fixedSession.Run(Ort::RunOptions{nullptr}, inputs.data(), &tensor, 1,
                                 outputs.data(), outputs.size());
                session_ = std::move(fixedSession);
                return;
            }
            catch (const Ort::Exception &)
            {
                if (accelerator_ != OrtAccelerator::None)
                {
                    throw;
                }
            }
            catch (const std::exception &)
            {
            }
        }
        if (fallbackSize <= 0)
        {
            throw std::runtime_error("Could not adapt the YuNet model input size.");
        }
        inputSize_ = fallbackSize;
        adoptOriginalSessionIfAccelerated(modelBytes);
    }

    FaceDetections YunetFaceDetector::detect(const cv::Mat &bgrImage, float scoreThreshold,
                                             float nmsThreshold)
    {
        if (bgrImage.empty())
        {
            return {};
        }
        auto prepared = prepare(bgrImage);
        const std::array<int64_t, 4> shape = {1, kChannels, inputSize_, inputSize_};
        auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto tensor = Ort::Value::CreateTensor<float>(
            memory, prepared.tensor.data(), prepared.tensor.size(), shape.data(), shape.size());
        auto outputs = session_.Run(Ort::RunOptions{nullptr}, inputNamePtrs_.data(), &tensor, 1,
                                    outputNamePtrs_.data(), outputNamePtrs_.size());
        return decode(outputs, prepared, scoreThreshold, nmsThreshold);
    }

    YunetFaceDetector::PreparedImage YunetFaceDetector::prepare(const cv::Mat &bgrImage) const
    {
        PreparedImage prepared;
        prepared.originalWidth = bgrImage.cols;
        prepared.originalHeight = bgrImage.rows;
        prepared.scale = std::min(static_cast<float>(inputSize_) / bgrImage.cols,
                                  static_cast<float>(inputSize_) / bgrImage.rows);
        const int width = std::max(1, static_cast<int>(std::round(bgrImage.cols * prepared.scale)));
        const int height = std::max(1, static_cast<int>(std::round(bgrImage.rows * prepared.scale)));
        cv::Mat resized;
        cv::resize(bgrImage, resized, {width, height});
        cv::Mat canvas(inputSize_, inputSize_, CV_8UC3, cv::Scalar(0, 0, 0));
        resized.copyTo(canvas(cv::Rect(0, 0, width, height)));

        prepared.tensor.resize(static_cast<std::size_t>(kChannels) * inputSize_ * inputSize_);
        const int plane = inputSize_ * inputSize_;
        for (int y = 0; y < inputSize_; ++y)
        {
            const auto *row = canvas.ptr<cv::Vec3b>(y);
            for (int x = 0; x < inputSize_; ++x)
            {
                const int offset = y * inputSize_ + x;
                prepared.tensor[offset] = row[x][0];
                prepared.tensor[plane + offset] = row[x][1];
                prepared.tensor[2 * plane + offset] = row[x][2];
            }
        }
        return prepared;
    }

    FaceDetections YunetFaceDetector::decode(const std::vector<Ort::Value> &outputs,
                                             const PreparedImage &prepared,
                                             float scoreThreshold,
                                             float nmsThreshold) const
    {
        FaceDetections detections;
        for (const int stride: kStrides)
        {
            const int rows = inputSize_ / stride;
            const int cols = inputSize_ / stride;
            const std::size_t count = static_cast<std::size_t>(rows) * cols;
            const std::string suffix = std::to_string(stride);
            const float *cls = tensorData(outputNamed(outputs, outputNames_, "cls_" + suffix),
                                          count, 1);
            const float *obj = tensorData(outputNamed(outputs, outputNames_, "obj_" + suffix),
                                          count, 1);
            const float *bbox = tensorData(outputNamed(outputs, outputNames_, "bbox_" + suffix),
                                           count * 4U, 4);
            if (cls == nullptr || obj == nullptr || bbox == nullptr)
            {
                throw std::runtime_error("YuNet output tensor shapes do not match the input size.");
            }

            for (int row = 0; row < rows; ++row)
            {
                for (int col = 0; col < cols; ++col)
                {
                    const auto index = static_cast<std::size_t>(row) * cols + col;
                    const float score = std::sqrt(std::clamp(cls[index], 0.0F, 1.0F) *
                                                  std::clamp(obj[index], 0.0F, 1.0F));
                    if (score < scoreThreshold)
                    {
                        continue;
                    }
                    const float centerX = (col + bbox[index * 4U]) * stride;
                    const float centerY = (row + bbox[index * 4U + 1U]) * stride;
                    const float width = std::exp(bbox[index * 4U + 2U]) * stride;
                    const float height = std::exp(bbox[index * 4U + 3U]) * stride;
                    float x = (centerX - width * 0.5F) / prepared.scale;
                    float y = (centerY - height * 0.5F) / prepared.scale;
                    float w = width / prepared.scale;
                    float h = height / prepared.scale;
                    x = std::clamp(x, 0.0F, static_cast<float>(prepared.originalWidth - 1));
                    y = std::clamp(y, 0.0F, static_cast<float>(prepared.originalHeight - 1));
                    const float right = std::clamp(x + w, x + 1.0F,
                                                   static_cast<float>(prepared.originalWidth));
                    const float bottom = std::clamp(y + h, y + 1.0F,
                                                    static_cast<float>(prepared.originalHeight));
                    detections.push_back({{x, y, right - x, bottom - y}, score});
                }
            }
        }

        if (detections.size() > kMaxCandidatesBeforeNms)
        {
            std::ranges::partial_sort(detections,
                                      detections.begin() + kMaxCandidatesBeforeNms,
                                      [](const FaceDetection &a, const FaceDetection &b)
                                      { return a.score > b.score; });
            detections.resize(kMaxCandidatesBeforeNms);
        }
        return nonMaxSuppression(std::move(detections), nmsThreshold);
    }
}
