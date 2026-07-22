#include "cloakframe/Mosaic.hpp"

#include <opencv2/core/version.hpp>
#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry/2d.hpp>
#endif
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <list>
#include <map>
#include <mutex>
#include <tuple>
#include <vector>

namespace cloakframe
{
    namespace
    {
        cv::Rect paddedRegion(const cv::Rect2f &box, int width, int height, float paddingRatio)
        {
            const double padding = std::isfinite(paddingRatio)
                                       ? std::max(0.0, static_cast<double>(paddingRatio))
                                       : 0.0;
            const double padX = static_cast<double>(box.width) * padding;
            const double padY = static_cast<double>(box.height) * padding;
            const double leftValue = std::clamp(
                static_cast<double>(box.x) - padX, 0.0, static_cast<double>(width));
            const double topValue = std::clamp(
                static_cast<double>(box.y) - padY, 0.0, static_cast<double>(height));
            const double rightValue = std::clamp(
                static_cast<double>(box.x) + box.width + padX,
                0.0, static_cast<double>(width));
            const double bottomValue = std::clamp(
                static_cast<double>(box.y) + box.height + padY,
                0.0, static_cast<double>(height));

            int x = static_cast<int>(std::floor(leftValue));
            int y = static_cast<int>(std::floor(topValue));
            int right = static_cast<int>(std::ceil(rightValue));
            int bottom = static_cast<int>(std::ceil(bottomValue));

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

        int availableInsetFor(const cv::Rect &region, const cv::Rect &detected)
        {
            return std::max(0, std::min({detected.x - region.x,
                                        detected.y - region.y,
                                        region.x + region.width - detected.x - detected.width,
                                        region.y + region.height - detected.y - detected.height}));
        }

        void fillRoundedRectangle(cv::Mat image, const cv::Rect &rect, int radius,
                                  const cv::Scalar &color)
        {
            radius = std::clamp(radius, 0, std::min(rect.width, rect.height) / 2);
            if (radius == 0)
            {
                cv::rectangle(image, rect, color, cv::FILLED, cv::LINE_AA);
                return;
            }

            cv::rectangle(image,
                          cv::Rect(rect.x + radius, rect.y,
                                   rect.width - 2 * radius, rect.height),
                          color, cv::FILLED, cv::LINE_AA);
            cv::rectangle(image,
                          cv::Rect(rect.x, rect.y + radius,
                                   rect.width, rect.height - 2 * radius),
                          color, cv::FILLED, cv::LINE_AA);
            for (const cv::Point center: {
                     cv::Point(rect.x + radius, rect.y + radius),
                     cv::Point(rect.x + rect.width - radius - 1, rect.y + radius),
                     cv::Point(rect.x + radius, rect.y + rect.height - radius - 1),
                     cv::Point(rect.x + rect.width - radius - 1,
                               rect.y + rect.height - radius - 1)})
            {
                cv::circle(image, center, radius, color, cv::FILLED, cv::LINE_AA);
            }
        }

        void stickerRegion(cv::Mat roi, int radiusLimit)
        {
            const int minEdge = std::min(roi.cols, roi.rows);
            if (minEdge < 3)
            {
                roi.setTo(cv::Scalar(40, 190, 245, 255));
                return;
            }

            const int outline = std::max(1, minEdge / 28);
            const int radius = std::clamp(minEdge / 5, 0, std::max(0, radiusLimit));
            const cv::Rect outer(0, 0, roi.cols, roi.rows);
            fillRoundedRectangle(roi, outer, radius, cv::Scalar(35, 45, 55, 255));

            const cv::Rect face(outline, outline,
                                std::max(1, roi.cols - 2 * outline),
                                std::max(1, roi.rows - 2 * outline));
            fillRoundedRectangle(roi, face, std::max(0, radius - outline),
                                 cv::Scalar(45, 205, 250, 255));

            const int eyeRadius = std::max(1, minEdge / 14);
            const int eyeY = static_cast<int>(std::lround(roi.rows * 0.39));
            const int leftEyeX = static_cast<int>(std::lround(roi.cols * 0.34));
            const int rightEyeX = static_cast<int>(std::lround(roi.cols * 0.66));
            const cv::Scalar ink(30, 40, 50, 255);
            cv::circle(roi, {leftEyeX, eyeY}, eyeRadius, ink, cv::FILLED, cv::LINE_AA);
            cv::circle(roi, {rightEyeX, eyeY}, eyeRadius, ink, cv::FILLED, cv::LINE_AA);

            const cv::Point mouthCenter(roi.cols / 2,
                                        static_cast<int>(std::lround(roi.rows * 0.56)));
            const cv::Size mouthAxes(std::max(2, roi.cols / 5),
                                     std::max(2, roi.rows / 7));
            cv::ellipse(roi, mouthCenter, mouthAxes, 0.0, 18.0, 162.0, ink,
                        std::max(1, minEdge / 22), cv::LINE_AA);
        }

        constexpr float kInnerTransitionRatio = 0.12F;
        constexpr float kOuterTransitionRatio = 0.10F;
        constexpr int kMinOuterTransition = 4;
        constexpr int kSizeQuantum = 4;

        int outerTransitionFor(const cv::Rect &region)
        {
            const int base = std::min(region.width, region.height);
            return std::max(kMinOuterTransition,
                            static_cast<int>(std::lround(
                                static_cast<float>(base) * kOuterTransitionRatio)));
        }

        int innerTransitionFor(const cv::Rect &region, const cv::Rect &detected)
        {
            const int available = availableInsetFor(region, detected);
            const int target = static_cast<int>(std::lround(
                static_cast<float>(std::min(region.width, region.height))
                * kInnerTransitionRatio));
            return std::min(available, target);
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

        cv::Mat softTransitionMask(const cv::Size &size, const cv::Rect &core, MaskShape shape,
                                   int innerTransition, int outerTransition)
        {
            constexpr std::size_t maxDistanceTransformPixels = 2U * 1024U * 1024U;
            const auto pixelCount = static_cast<std::size_t>(size.width) *
                                    static_cast<std::size_t>(size.height);
            if (pixelCount > maxDistanceTransformPixels)
            {
                cv::Mat mask(size, CV_8UC1);
                const float transition = static_cast<float>(innerTransition + outerTransition);
                const float centerX = static_cast<float>(core.x) +
                                      static_cast<float>(core.width - 1) * 0.5F;
                const float centerY = static_cast<float>(core.y) +
                                      static_cast<float>(core.height - 1) * 0.5F;
                const float radiusX = std::max(0.5F, static_cast<float>(core.width) * 0.5F);
                const float radiusY = std::max(0.5F, static_cast<float>(core.height) * 0.5F);
                const float radiusScale = std::min(radiusX, radiusY);

                cv::parallel_for_(cv::Range(0, size.height), [&](const cv::Range &range)
                {
                    for (int y = range.start; y < range.end; ++y)
                    {
                        auto *row = mask.ptr<unsigned char>(y);
                        for (int x = 0; x < size.width; ++x)
                        {
                            float signedDistance = 0.0F;
                            if (shape == MaskShape::Ellipse)
                            {
                                const float dx = (static_cast<float>(x) - centerX) / radiusX;
                                const float dy = (static_cast<float>(y) - centerY) / radiusY;
                                signedDistance = (1.0F - std::sqrt(dx * dx + dy * dy)) *
                                                 radiusScale;
                            }
                            else
                            {
                                const float outsideX = std::max(
                                    {static_cast<float>(core.x - x), 0.0F,
                                     static_cast<float>(x - (core.x + core.width - 1))});
                                const float outsideY = std::max(
                                    {static_cast<float>(core.y - y), 0.0F,
                                     static_cast<float>(y - (core.y + core.height - 1))});
                                if (outsideX > 0.0F || outsideY > 0.0F)
                                {
                                    signedDistance = -std::sqrt(outsideX * outsideX +
                                                                outsideY * outsideY);
                                }
                                else
                                {
                                    signedDistance = static_cast<float>(std::min(
                                        {x - core.x + 1, y - core.y + 1,
                                         core.x + core.width - x,
                                         core.y + core.height - y}));
                                }
                            }

                            float ramp = (signedDistance + static_cast<float>(outerTransition)) /
                                         transition;
                            ramp = std::clamp(ramp, 0.0F, 1.0F);
                            ramp = ramp * ramp * (3.0F - 2.0F * ramp);
                            row[x] = cv::saturate_cast<unsigned char>(ramp * 255.0F);
                        }
                    }
                });
                return mask;
            }

            cv::Mat inside(size, CV_8UC1, cv::Scalar(0));
            if (shape == MaskShape::Ellipse)
            {
                cv::ellipse(inside,
                            cv::Point(core.x + core.width / 2, core.y + core.height / 2),
                            cv::Size(core.width / 2, core.height / 2),
                            0.0, 0.0, 360.0, cv::Scalar(255), cv::FILLED, cv::LINE_8);
            }
            else
            {
                cv::rectangle(inside, core, cv::Scalar(255), cv::FILLED);
            }

            cv::Mat outside;
            cv::bitwise_not(inside, outside);
            cv::Mat insideDistance;
            cv::Mat outsideDistance;
            cv::distanceTransform(inside, insideDistance, cv::DIST_L2, cv::DIST_MASK_PRECISE);
            cv::distanceTransform(outside, outsideDistance, cv::DIST_L2, cv::DIST_MASK_PRECISE);

            const float transition = static_cast<float>(innerTransition + outerTransition);
            cv::Mat ramp = (insideDistance - outsideDistance
                            + static_cast<float>(outerTransition)) / transition;
            ramp = cv::min(ramp, 1.0F);
            ramp = cv::max(ramp, 0.0F);
            return ramp.mul(ramp).mul(3.0F - 2.0F * ramp);
        }

        using MaskKey = std::tuple<int, int, int, int, int, int, int, int, int>;

        struct MaskCacheEntry
        {
            cv::Mat mask;
            std::size_t bytes = 0;
            std::list<MaskKey>::iterator recency;
        };

        std::mutex g_maskCacheMutex;
        std::mutex g_maskComputationMutex;
        std::map<MaskKey, MaskCacheEntry> g_maskCache;
        std::list<MaskKey> g_maskCacheRecency;
        std::size_t g_maskCacheBytes = 0;
        constexpr std::size_t kMaskCacheEntryCap = 2048;
        constexpr std::size_t kMaskCacheByteCap = 32U * 1024U * 1024U;
        constexpr std::size_t kMaskCacheSingleEntryCap = 8U * 1024U * 1024U;

        cv::Mat cachedSoftTransitionMask(const cv::Size &size, const cv::Rect &core,
                                         MaskShape shape, int innerTransition,
                                         int outerTransition)
        {
            const MaskKey key{size.width, size.height, core.x, core.y,
                              core.width, core.height, static_cast<int>(shape),
                              innerTransition, outerTransition};
            {
                const std::lock_guard<std::mutex> lock(g_maskCacheMutex);
                const auto it = g_maskCache.find(key);
                if (it != g_maskCache.end())
                {
                    g_maskCacheRecency.splice(g_maskCacheRecency.begin(),
                                              g_maskCacheRecency,
                                              it->second.recency);
                    return it->second.mask;
                }
            }

            const std::lock_guard<std::mutex> computationLock(g_maskComputationMutex);
            {
                const std::lock_guard<std::mutex> lock(g_maskCacheMutex);
                const auto it = g_maskCache.find(key);
                if (it != g_maskCache.end())
                {
                    g_maskCacheRecency.splice(g_maskCacheRecency.begin(),
                                              g_maskCacheRecency,
                                              it->second.recency);
                    return it->second.mask;
                }
            }

            cv::Mat mask = softTransitionMask(size, core, shape, innerTransition,
                                              outerTransition);

            const std::size_t maskBytes = mask.total() * mask.elemSize();
            if (maskBytes == 0 || maskBytes > kMaskCacheSingleEntryCap)
            {
                return mask;
            }

            const std::lock_guard<std::mutex> lock(g_maskCacheMutex);
            const auto existing = g_maskCache.find(key);
            if (existing != g_maskCache.end())
            {
                g_maskCacheRecency.splice(g_maskCacheRecency.begin(),
                                          g_maskCacheRecency,
                                          existing->second.recency);
                return existing->second.mask;
            }

            while (!g_maskCacheRecency.empty() &&
                   (g_maskCache.size() >= kMaskCacheEntryCap ||
                    g_maskCacheBytes > kMaskCacheByteCap - maskBytes))
            {
                const auto oldestKey = g_maskCacheRecency.back();
                const auto oldest = g_maskCache.find(oldestKey);
                if (oldest != g_maskCache.end())
                {
                    g_maskCacheBytes -= oldest->second.bytes;
                    g_maskCache.erase(oldest);
                }
                g_maskCacheRecency.pop_back();
            }

            bool recencyAdded = false;
            try
            {
                g_maskCacheRecency.push_front(key);
                recencyAdded = true;
                const auto [inserted, added] = g_maskCache.emplace(
                    key, MaskCacheEntry{mask, maskBytes, g_maskCacheRecency.begin()});
                if (!added)
                {
                    g_maskCacheRecency.pop_front();
                    g_maskCacheRecency.splice(g_maskCacheRecency.begin(),
                                              g_maskCacheRecency,
                                              inserted->second.recency);
                    return inserted->second.mask;
                }
                g_maskCacheBytes += maskBytes;
            }
            catch (...)
            {
                if (recencyAdded)
                {
                    g_maskCacheRecency.pop_front();
                }
            }
            return mask;
        }

        template<typename Pixel, typename Alpha>
        void blendWithMaskTyped(cv::Mat roi, const cv::Mat &anonymized,
                                const cv::Mat &alpha, const double alphaScale)
        {
            const int channels = roi.channels();
            cv::parallel_for_(cv::Range(0, roi.rows), [&](const cv::Range &range)
            {
                for (int y = range.start; y < range.end; ++y)
                {
                    auto *destination = roi.ptr<Pixel>(y);
                    const auto *source = anonymized.ptr<Pixel>(y);
                    const auto *weights = alpha.ptr<Alpha>(y);
                    for (int x = 0; x < roi.cols; ++x)
                    {
                        const double weight = static_cast<double>(weights[x]) * alphaScale;
                        const int offset = x * channels;
                        for (int channel = 0; channel < channels; ++channel)
                        {
                            const double original = static_cast<double>(destination[offset + channel]);
                            const double changed = static_cast<double>(source[offset + channel]);
                            destination[offset + channel] = cv::saturate_cast<Pixel>(
                                original + (changed - original) * weight);
                        }
                    }
                }
            });
        }

        template<typename Pixel>
        void blendWithMaskDepth(cv::Mat roi, const cv::Mat &anonymized, const cv::Mat &alpha)
        {
            if (alpha.depth() == CV_8U)
            {
                blendWithMaskTyped<Pixel, unsigned char>(roi, anonymized, alpha, 1.0 / 255.0);
            }
            else
            {
                blendWithMaskTyped<Pixel, float>(roi, anonymized, alpha, 1.0);
            }
        }

        void blendWithMask(cv::Mat roi, const cv::Mat &anonymized, const cv::Mat &alpha)
        {
            switch (roi.depth())
            {
                case CV_8U:
                    blendWithMaskDepth<unsigned char>(roi, anonymized, alpha);
                    break;
                case CV_8S:
                    blendWithMaskDepth<signed char>(roi, anonymized, alpha);
                    break;
                case CV_16U:
                    blendWithMaskDepth<unsigned short>(roi, anonymized, alpha);
                    break;
                case CV_16S:
                    blendWithMaskDepth<short>(roi, anonymized, alpha);
                    break;
                case CV_32S:
                    blendWithMaskDepth<int>(roi, anonymized, alpha);
                    break;
                case CV_32F:
                    blendWithMaskDepth<float>(roi, anonymized, alpha);
                    break;
                case CV_64F:
                    blendWithMaskDepth<double>(roi, anonymized, alpha);
                    break;
                default:
                    CV_Error(cv::Error::StsUnsupportedFormat, "Unsupported image depth");
            }
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
            case AnonymizationMethod::Sticker:
                stickerRegion(roi, std::min(roi.cols, roi.rows) / 5);
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
            if (!isValidFaceDetection(detection))
            {
                continue;
            }
            const cv::Rect roiRect = paddedRegion(detection.box, width, height, paddingRatio);

            if (method == AnonymizationMethod::Sticker)
            {
                const cv::Rect detectedRect = paddedRegion(detection.box, width, height, 0.0F);
                stickerRegion(image(roiRect), availableInsetFor(roiRect, detectedRect) * 2);
                continue;
            }

            if (softEdges)
            {
                const cv::Rect qRect = quantizeRegionSize(roiRect, kSizeQuantum);
                const cv::Rect detectedRect = paddedRegion(detection.box, width, height, 0.0F);
                const int innerTransition = innerTransitionFor(qRect, detectedRect);
                const int outerTransition = outerTransitionFor(qRect);
                const int canonX = qRect.x - outerTransition;
                const int canonY = qRect.y - outerTransition;

                const int outerX = std::max(0, canonX);
                const int outerY = std::max(0, canonY);
                const int outerRight = std::min(width, qRect.x + qRect.width + outerTransition);
                const int outerBottom = std::min(height, qRect.y + qRect.height + outerTransition);
                if (outerRight <= outerX || outerBottom <= outerY)
                {
                    continue;
                }
                const cv::Rect outer(outerX, outerY, outerRight - outerX, outerBottom - outerY);
                cv::Mat roi = image(outer);

                cv::Mat anonymized = roi.clone();
                applyEffect(anonymized, method, blockSize);

                const cv::Size canonicalSize(qRect.width + 2 * outerTransition,
                                             qRect.height + 2 * outerTransition);
                const cv::Rect core(outerTransition, outerTransition,
                                    qRect.width, qRect.height);
                const cv::Mat canonical = cachedSoftTransitionMask(
                    canonicalSize, core, shape, innerTransition, outerTransition);
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
