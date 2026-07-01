#include "faceveil/Mosaic.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>

namespace faceveil
{
    namespace
    {
        cv::Rect paddedRegion(const cv::Rect2f &box, int width, int height, float paddingRatio)
        {
            const float padX = box.width * paddingRatio;
            const float padY = box.height * paddingRatio;

            int x = static_cast<int>(std::floor(box.x - padX));
            int y = static_cast<int>(std::floor(box.y - padY));
            int right = static_cast<int>(std::ceil(box.x + box.width + padX));
            int bottom = static_cast<int>(std::ceil(box.y + box.height + padY));

            x = std::clamp(x, 0, width - 1);
            y = std::clamp(y, 0, height - 1);
            right = std::clamp(right, x + 1, width);
            bottom = std::clamp(bottom, y + 1, height);

            return cv::Rect(x, y, right - x, bottom - y);
        }

        void mosaicRegion(cv::Mat roi, int blockSize)
        {
            const int smallWidth = std::clamp(roi.cols / blockSize, 1, std::max(1, roi.cols / 2));
            const int smallHeight = std::clamp(roi.rows / blockSize, 1, std::max(1, roi.rows / 2));

            cv::Mat small;
            cv::resize(roi, small, cv::Size(smallWidth, smallHeight), 0.0, 0.0, cv::INTER_LINEAR);
            cv::resize(small, roi, roi.size(), 0.0, 0.0, cv::INTER_NEAREST);
        }

        void blurRegion(cv::Mat roi)
        {
            int kernel = std::min(roi.cols, roi.rows) / 2;
            if (kernel < 1)
            {
                kernel = 1;
            }
            if (kernel % 2 == 0)
            {
                ++kernel;
            }
            const double sigma = static_cast<double>(kernel) / 3.0;
            cv::GaussianBlur(roi, roi, cv::Size(kernel, kernel), sigma);
        }

        void fillRegion(cv::Mat roi)
        {
            roi.setTo(cv::Scalar(0, 0, 0));
        }
    }

    void applyAnonymization(cv::Mat &image, const FaceDetections &detections,
                            AnonymizationMethod method, int blockSize, float paddingRatio)
    {
        if (image.empty())
        {
            return;
        }

        const int width = image.cols;
        const int height = image.rows;

        blockSize = std::max(blockSize, 2);

        for (const auto &detection: detections)
        {
            const cv::Rect roiRect = paddedRegion(detection.box, width, height, paddingRatio);
            cv::Mat roi = image(roiRect);

            switch (method)
            {
                case AnonymizationMethod::Blur:
                    blurRegion(roi);
                    break;
                case AnonymizationMethod::Fill:
                    fillRegion(roi);
                    break;
                case AnonymizationMethod::Mosaic:
                default:
                    mosaicRegion(roi, blockSize);
                    break;
            }
        }
    }

    void applyMosaic(cv::Mat &image, const FaceDetections &detections, int blockSize, float paddingRatio)
    {
        applyAnonymization(image, detections, AnonymizationMethod::Mosaic, blockSize, paddingRatio);
    }
}
