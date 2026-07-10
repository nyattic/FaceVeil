#include "redactly/Mosaic.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

namespace redactly
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

        constexpr int kMaxMosaicCells = 12;

        void mosaicRegion(cv::Mat roi, int blockSize)
        {
            const int widthCap = std::min(kMaxMosaicCells, std::max(1, roi.cols / 2));
            const int heightCap = std::min(kMaxMosaicCells, std::max(1, roi.rows / 2));
            const int smallWidth = std::clamp(roi.cols / blockSize, 1, widthCap);
            const int smallHeight = std::clamp(roi.rows / blockSize, 1, heightCap);

            cv::Mat small;
            cv::resize(roi, small, cv::Size(smallWidth, smallHeight), 0.0, 0.0, cv::INTER_LINEAR);
            cv::resize(small, roi, roi.size(), 0.0, 0.0, cv::INTER_NEAREST);
        }

        int oddKernelFor(double sigma)
        {
            int kernel = static_cast<int>(std::lround(sigma * 3.0)) | 1;
            return std::max(3, kernel);
        }

        void blurRegion(cv::Mat roi)
        {
            const int minEdge = std::min(roi.cols, roi.rows);
            if (minEdge < 2)
            {
                return;
            }

            const double sigma = static_cast<double>(minEdge) / 6.0;
            int down = std::max(1, static_cast<int>(std::lround(sigma / 2.5)));
            down = std::min(down, minEdge / 2);
            if (down <= 1)
            {
                cv::GaussianBlur(roi, roi, cv::Size(oddKernelFor(sigma), oddKernelFor(sigma)), sigma);
                return;
            }

            cv::Mat small;
            const cv::Size smallSize(std::max(1, roi.cols / down), std::max(1, roi.rows / down));
            cv::resize(roi, small, smallSize, 0.0, 0.0, cv::INTER_AREA);
            const double smallSigma = sigma / down;
            cv::GaussianBlur(small, small, cv::Size(oddKernelFor(smallSigma), oddKernelFor(smallSigma)),
                             smallSigma);
            cv::resize(small, roi, roi.size(), 0.0, 0.0, cv::INTER_LINEAR);
        }

        void fillRegion(cv::Mat roi)
        {
            roi.setTo(cv::Scalar(0, 0, 0));
        }

        constexpr float kFeatherRatio = 0.15F;
        constexpr int kMinFeather = 4;
        constexpr int kSizeQuantum = 4;

        int featherSizeFor(const cv::Rect &region)
        {
            const int base = std::min(region.width, region.height);
            return std::max(kMinFeather, static_cast<int>(std::lround(static_cast<float>(base) * kFeatherRatio)));
        }

        cv::Rect quantizeRegionSize(const cv::Rect &region, int quantum)
        {
            if (quantum <= 1)
            {
                return region;
            }
            const int qw = ((region.width + quantum - 1) / quantum) * quantum;
            const int qh = ((region.height + quantum - 1) / quantum) * quantum;
            return {region.x, region.y, qw, qh};
        }

        cv::Mat softCoreMask(const cv::Size &size, const cv::Rect &core, MaskShape shape, int feather)
        {
            cv::Mat outside(size, CV_8UC1, cv::Scalar(255));
            if (shape == MaskShape::Ellipse)
            {
                cv::ellipse(outside,
                            cv::Point(core.x + core.width / 2, core.y + core.height / 2),
                            cv::Size(core.width / 2, core.height / 2),
                            0.0, 0.0, 360.0, cv::Scalar(0), cv::FILLED, cv::LINE_8);
            }
            else
            {
                cv::rectangle(outside, core, cv::Scalar(0), cv::FILLED);
            }

            cv::Mat distance;
            cv::distanceTransform(outside, distance, cv::DIST_L2, cv::DIST_MASK_PRECISE);

            cv::Mat ramp = 1.0F - distance / static_cast<float>(feather);
            ramp = cv::min(ramp, 1.0F);
            ramp = cv::max(ramp, 0.0F);
            return ramp.mul(ramp).mul(3.0F - 2.0F * ramp);
        }

        using MaskKey = std::tuple<int, int, int, int, int, int, int, int>;

        std::mutex g_maskCacheMutex;
        std::map<MaskKey, cv::Mat> g_maskCache;
        constexpr std::size_t kMaskCacheCap = 2048;

        cv::Mat cachedSoftCoreMask(const cv::Size &size, const cv::Rect &core, MaskShape shape, int feather)
        {
            const MaskKey key{size.width, size.height, core.x, core.y,
                              core.width, core.height, static_cast<int>(shape), feather};
            {
                const std::lock_guard<std::mutex> lock(g_maskCacheMutex);
                const auto it = g_maskCache.find(key);
                if (it != g_maskCache.end())
                {
                    return it->second;
                }
            }

            cv::Mat mask = softCoreMask(size, core, shape, feather);

            const std::lock_guard<std::mutex> lock(g_maskCacheMutex);
            if (g_maskCache.size() >= kMaskCacheCap)
            {
                const std::size_t drop = kMaskCacheCap / 4;
                auto it = g_maskCache.begin();
                for (std::size_t i = 0; i < drop && it != g_maskCache.end(); ++i)
                {
                    it = g_maskCache.erase(it);
                }
            }
            g_maskCache.emplace(key, mask);
            return mask;
        }

        void blendWithMask(cv::Mat roi, const cv::Mat &anonymized, const cv::Mat &alpha)
        {
            cv::Mat roiFloat;
            cv::Mat anonymizedFloat;
            roi.convertTo(roiFloat, CV_32F);
            anonymized.convertTo(anonymizedFloat, CV_32F);

            const std::vector<cv::Mat> alphaChannels(static_cast<size_t>(roi.channels()), alpha);
            cv::Mat alphaMerged;
            cv::merge(alphaChannels, alphaMerged);

            cv::Mat blended = roiFloat + (anonymizedFloat - roiFloat).mul(alphaMerged);
            blended.convertTo(roi, roi.type());
        }
    }

    void applyEffect(cv::Mat roi, AnonymizationMethod method, int blockSize)
    {
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

    void applyAnonymization(cv::Mat &image, const FaceDetections &detections,
                            AnonymizationMethod method, int blockSize, float paddingRatio,
                            MaskShape shape, bool softEdges)
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

            if (softEdges)
            {
                const cv::Rect qRect = quantizeRegionSize(roiRect, kSizeQuantum);
                const int feather = featherSizeFor(qRect);
                const int canonX = qRect.x - feather;
                const int canonY = qRect.y - feather;

                const int outerX = std::max(0, canonX);
                const int outerY = std::max(0, canonY);
                const int outerRight = std::min(width, qRect.x + qRect.width + feather);
                const int outerBottom = std::min(height, qRect.y + qRect.height + feather);
                if (outerRight <= outerX || outerBottom <= outerY)
                {
                    continue;
                }
                const cv::Rect outer(outerX, outerY, outerRight - outerX, outerBottom - outerY);
                cv::Mat roi = image(outer);

                cv::Mat anonymized = roi.clone();
                applyEffect(anonymized, method, blockSize);

                const cv::Size canonicalSize(qRect.width + 2 * feather, qRect.height + 2 * feather);
                const cv::Rect core(feather, feather, qRect.width, qRect.height);
                const cv::Mat canonical = cachedSoftCoreMask(canonicalSize, core, shape, feather);
                const cv::Rect slice(outerX - canonX, outerY - canonY, outer.width, outer.height);
                blendWithMask(roi, anonymized, canonical(slice));
                continue;
            }

            cv::Mat roi = image(roiRect);

            if (shape == MaskShape::Ellipse)
            {
                cv::Mat masked = roi.clone();
                applyEffect(masked, method, blockSize);

                cv::Mat mask(roi.size(), CV_8UC1, cv::Scalar(0));
                cv::ellipse(mask,
                            cv::Point(roi.cols / 2, roi.rows / 2),
                            cv::Size(roi.cols / 2, roi.rows / 2),
                            0.0, 0.0, 360.0, cv::Scalar(255), cv::FILLED, cv::LINE_AA);
                masked.copyTo(roi, mask);
            }
            else
            {
                applyEffect(roi, method, blockSize);
            }
        }
    }

    void applyMosaic(cv::Mat &image, const FaceDetections &detections, int blockSize, float paddingRatio)
    {
        applyAnonymization(image, detections, AnonymizationMethod::Mosaic, blockSize, paddingRatio);
    }
}
