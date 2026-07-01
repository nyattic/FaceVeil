#include "faceveil/ScrfdFaceDetector.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace faceveil
{
    namespace
    {
        constexpr std::array<int, 3> kStrides = {8, 16, 32};
        constexpr int kChannels = 3;
        constexpr int kMaxAnchorsPerLocation = 2;
        constexpr size_t kMaxCandidatesBeforeNms = 2000;

        cv::Rect2f distanceToBox(const cv::Point2f &center, const float *distances)
        {
            const float left = distances[0];
            const float top = distances[1];
            const float right = distances[2];
            const float bottom = distances[3];
            return {center.x - left, center.y - top, left + right, top + bottom};
        }
    }

    ScrfdFaceDetector::ScrfdFaceDetector(const std::string &modelPath, int inputSize)
        : inputSize_(inputSize),
          env_(ORT_LOGGING_LEVEL_WARNING, "FaceVeil"),
          sessionOptions_(),
          session_(nullptr)
    {
        sessionOptions_.SetIntraOpNumThreads(1);
        sessionOptions_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        const std::filesystem::path modelFsPath = modelPath;
        session_ = Ort::Session(env_, modelFsPath.c_str(), sessionOptions_);

        Ort::AllocatorWithDefaultOptions allocator;

        const auto inputCount = session_.GetInputCount();
        inputNames_.reserve(inputCount);
        for (size_t i = 0; i < inputCount; ++i)
        {
            auto name = session_.GetInputNameAllocated(i, allocator);
            inputNames_.emplace_back(name.get());
        }

        const auto outputCount = session_.GetOutputCount();
        outputNames_.reserve(outputCount);
        for (size_t i = 0; i < outputCount; ++i)
        {
            auto name = session_.GetOutputNameAllocated(i, allocator);
            outputNames_.emplace_back(name.get());
        }

        if (inputNames_.empty() || outputNames_.size() < 6)
        {
            throw std::runtime_error("The selected model does not look like an SCRFD ONNX model.");
        }

        const auto inputTypeInfo = session_.GetInputTypeInfo(0);
        const auto inputInfo = inputTypeInfo.GetTensorTypeAndShapeInfo();
        if (inputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
        {
            throw std::runtime_error("SCRFD model input must be a float tensor.");
        }
        const auto inputShape = inputInfo.GetShape();
        if (inputShape.size() != 4)
        {
            throw std::runtime_error("SCRFD model input must be a 4D NCHW tensor.");
        }
        const auto dimensionMatches = [](int64_t actual, int64_t expected)
        {
            return actual < 0 || actual == expected;
        };
        if (!dimensionMatches(inputShape[0], 1) ||
            !dimensionMatches(inputShape[1], kChannels) ||
            !dimensionMatches(inputShape[2], inputSize_) ||
            !dimensionMatches(inputShape[3], inputSize_))
        {
            throw std::runtime_error("SCRFD model input shape must be compatible with [1, 3, 640, 640].");
        }

        for (size_t i = 0; i < std::min<size_t>(outputNames_.size(), kStrides.size() * 2U); ++i)
        {
            const auto outputTypeInfo = session_.GetOutputTypeInfo(i);
            const auto outputInfo = outputTypeInfo.GetTensorTypeAndShapeInfo();
            if (outputInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            {
                throw std::runtime_error("SCRFD model outputs must be float tensors.");
            }
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

    FaceDetections ScrfdFaceDetector::detect(const cv::Mat &bgrImage, float scoreThreshold, float nmsThreshold)
    {
        if (bgrImage.empty())
        {
            return {};
        }

        auto prepared = prepare(bgrImage);
        std::array<int64_t, 4> inputShape = {1, kChannels, inputSize_, inputSize_};

        Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memoryInfo,
            prepared.tensor.data(),
            prepared.tensor.size(),
            inputShape.data(),
            inputShape.size());

        auto outputs = session_.Run(Ort::RunOptions{nullptr},
                                    inputNamePtrs_.data(),
                                    &inputTensor,
                                    1,
                                    outputNamePtrs_.data(),
                                    outputNamePtrs_.size());

        return decode(outputs, prepared, scoreThreshold, nmsThreshold);
    }

    ScrfdFaceDetector::PreparedImage ScrfdFaceDetector::prepare(const cv::Mat &bgrImage) const
    {
        PreparedImage prepared;
        prepared.originalWidth = bgrImage.cols;
        prepared.originalHeight = bgrImage.rows;

        const float scale = std::min(static_cast<float>(inputSize_) / static_cast<float>(bgrImage.cols),
                                     static_cast<float>(inputSize_) / static_cast<float>(bgrImage.rows));
        prepared.scale = scale;
        prepared.resizedWidth = std::max(1, static_cast<int>(std::round(static_cast<float>(bgrImage.cols) * scale)));
        prepared.resizedHeight = std::max(1, static_cast<int>(std::round(static_cast<float>(bgrImage.rows) * scale)));

        cv::Mat resized;
        cv::resize(bgrImage, resized, cv::Size(prepared.resizedWidth, prepared.resizedHeight));

        cv::Mat canvas(inputSize_, inputSize_, CV_8UC3, cv::Scalar(0, 0, 0));
        resized.copyTo(canvas(cv::Rect(0, 0, resized.cols, resized.rows)));

        cv::Mat rgb;
        cv::cvtColor(canvas, rgb, cv::COLOR_BGR2RGB);

        prepared.tensor.resize(kChannels * inputSize_ * inputSize_);
        const int planeSize = inputSize_ * inputSize_;
        for (int y = 0; y < inputSize_; ++y)
        {
            const auto *row = rgb.ptr<cv::Vec3b>(y);
            for (int x = 0; x < inputSize_; ++x)
            {
                const int offset = y * inputSize_ + x;
                prepared.tensor[offset] = (static_cast<float>(row[x][0]) - 127.5F) / 128.0F;
                prepared.tensor[planeSize + offset] = (static_cast<float>(row[x][1]) - 127.5F) / 128.0F;
                prepared.tensor[2 * planeSize + offset] = (static_cast<float>(row[x][2]) - 127.5F) / 128.0F;
            }
        }

        return prepared;
    }

    FaceDetections ScrfdFaceDetector::decode(const std::vector<Ort::Value> &outputs,
                                             const PreparedImage &prepared,
                                             float scoreThreshold,
                                             float nmsThreshold) const
    {
        constexpr auto featureMapCount = kStrides.size();
        if (outputs.size() < featureMapCount * 2U)
        {
            throw std::runtime_error("SCRFD output tensor count is too small.");
        }

        struct StrideOutput
        {
            const Ort::Value *value = nullptr;
            size_t elementCount = 0;
        };

        std::vector<StrideOutput> scoreOutputs;
        std::vector<StrideOutput> bboxOutputs;
        for (const auto &output: outputs)
        {
            if (!output.IsTensor())
            {
                continue;
            }
            const auto info = output.GetTensorTypeAndShapeInfo();
            if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
            {
                continue;
            }
            const auto shape = info.GetShape();
            if (shape.empty())
            {
                continue;
            }
            const auto elementCount = info.GetElementCount();
            if (elementCount == 0 ||
                elementCount > static_cast<size_t>(std::numeric_limits<int>::max()))
            {
                continue;
            }
            const auto lastDim = shape.back();
            if (lastDim == 1)
            {
                scoreOutputs.push_back({&output, elementCount});
            }
            else if (lastDim == 4)
            {
                bboxOutputs.push_back({&output, elementCount});
            }
        }

        FaceDetections detections;

        for (size_t index = 0; index < featureMapCount; ++index)
        {
            const int stride = kStrides[index];
            const int featureHeight = inputSize_ / stride;
            const int featureWidth = inputSize_ / stride;
            auto anchors = anchorCenters(featureHeight, featureWidth, stride);
            const int baseAnchorCount = static_cast<int>(anchors.size());
            if (baseAnchorCount <= 0)
            {
                continue;
            }

            const StrideOutput *scoreMatch = nullptr;
            for (const auto &candidate: scoreOutputs)
            {
                if (candidate.elementCount == static_cast<size_t>(baseAnchorCount) ||
                    candidate.elementCount ==
                        static_cast<size_t>(baseAnchorCount) * kMaxAnchorsPerLocation)
                {
                    scoreMatch = &candidate;
                    break;
                }
            }

            const StrideOutput *bboxMatch = nullptr;
            if (scoreMatch != nullptr)
            {
                for (const auto &candidate: bboxOutputs)
                {
                    if (candidate.elementCount == scoreMatch->elementCount * 4U)
                    {
                        bboxMatch = &candidate;
                        break;
                    }
                }
            }

            if (scoreMatch == nullptr || bboxMatch == nullptr)
            {
                throw std::runtime_error(
                    "Could not match SCRFD score and box outputs for this model.");
            }

            const auto scoreCount = static_cast<int>(scoreMatch->elementCount);
            const int maxExpectedScores = baseAnchorCount * kMaxAnchorsPerLocation;
            if (scoreCount > maxExpectedScores)
            {
                throw std::runtime_error("SCRFD output tensor shape is unexpectedly large.");
            }
            if (scoreCount > baseAnchorCount)
            {
                const int repeats = std::max(1, scoreCount / baseAnchorCount);
                std::vector<cv::Point2f> repeatedAnchors;
                repeatedAnchors.reserve(static_cast<size_t>(baseAnchorCount) * static_cast<size_t>(repeats));
                for (const auto &anchor: anchors)
                {
                    for (int repeat = 0; repeat < repeats; ++repeat)
                    {
                        repeatedAnchors.push_back(anchor);
                    }
                }
                anchors = std::move(repeatedAnchors);
            }

            const auto *scores = scoreMatch->value->GetTensorData<float>();
            const auto *boxes = bboxMatch->value->GetTensorData<float>();
            if (scores == nullptr || boxes == nullptr)
            {
                continue;
            }
            const int anchorsToRead = std::min(scoreCount, static_cast<int>(anchors.size()));

            for (int i = 0; i < anchorsToRead; ++i)
            {
                const float score = scores[i];
                if (score < scoreThreshold)
                {
                    continue;
                }

                std::array<float, 4> distances = {
                    boxes[i * 4] * static_cast<float>(stride),
                    boxes[i * 4 + 1] * static_cast<float>(stride),
                    boxes[i * 4 + 2] * static_cast<float>(stride),
                    boxes[i * 4 + 3] * static_cast<float>(stride),
                };
                const auto modelBox = distanceToBox(anchors[i], distances.data());
                float x = modelBox.x / prepared.scale;
                float y = modelBox.y / prepared.scale;
                float width = modelBox.width / prepared.scale;
                float height = modelBox.height / prepared.scale;

                x = std::clamp(x, 0.0F, static_cast<float>(prepared.originalWidth - 1));
                y = std::clamp(y, 0.0F, static_cast<float>(prepared.originalHeight - 1));
                const float right = std::clamp(x + width, x + 1.0F, static_cast<float>(prepared.originalWidth));
                const float bottom = std::clamp(y + height, y + 1.0F, static_cast<float>(prepared.originalHeight));
                detections.push_back({cv::Rect2f(x, y, right - x, bottom - y), score});
            }
        }

        if (detections.size() > kMaxCandidatesBeforeNms)
        {
            std::ranges::partial_sort(detections,
                                      detections.begin() + static_cast<std::ptrdiff_t>(kMaxCandidatesBeforeNms),
                                      [](const FaceDetection &a, const FaceDetection &b)
            {
                return a.score > b.score;
            });
            detections.resize(kMaxCandidatesBeforeNms);
        }

        return nonMaxSuppression(std::move(detections), nmsThreshold);
    }

    std::vector<cv::Point2f> ScrfdFaceDetector::anchorCenters(int featureHeight, int featureWidth, int stride)
    {
        std::vector<cv::Point2f> anchors;
        anchors.reserve(static_cast<size_t>(featureHeight) * static_cast<size_t>(featureWidth));
        for (int y = 0; y < featureHeight; ++y)
        {
            for (int x = 0; x < featureWidth; ++x)
            {
                anchors.emplace_back(static_cast<float>(x * stride), static_cast<float>(y * stride));
            }
        }
        return anchors;
    }

    float ScrfdFaceDetector::intersectionOverUnion(const cv::Rect2f &a, const cv::Rect2f &b)
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

    FaceDetections ScrfdFaceDetector::nonMaxSuppression(FaceDetections detections, float threshold)
    {
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
