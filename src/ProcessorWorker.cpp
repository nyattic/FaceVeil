#include "redactly/ProcessorWorker.hpp"

#include "redactly/ImageIo.hpp"
#include "redactly/ImageScanner.hpp"
#include "redactly/PathSafety.hpp"
#include "redactly/PathUtil.hpp"
#include "redactly/PlateDetector.hpp"
#include "redactly/Mosaic.hpp"
#include "redactly/ReviewTypes.hpp"
#include "redactly/ScrfdFaceDetector.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QImageReader>
#include <QMetaObject>
#include <QRectF>
#include <QSize>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <random>
#include <system_error>
#include <unordered_map>

namespace redactly
{
    namespace
    {
        constexpr std::uintmax_t kMaxInputFileBytes = 1ULL << 30;
        constexpr long long kMaxPixelCount = 200LL * 1000LL * 1000LL;

        constexpr int kReviewMaxLongEdge = 1600;

        struct ImageDimensionCheck
        {
            bool ok = false;
            QString reason;
            QSize size;
        };

        ImageDimensionCheck inspectImageDimensions(const std::filesystem::path &source)
        {
            QImageReader reader(pathToQString(source));
            reader.setAutoTransform(false);

            const QSize size = reader.size();
            if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
            {
                return {true, {}, {}};
            }

            const long long pixelCount =
                    static_cast<long long>(size.width()) * static_cast<long long>(size.height());
            if (pixelCount > kMaxPixelCount)
            {
                return {
                    false,
                    QCoreApplication::translate("redactly::ProcessorWorker", "image too large, %1 x %2")
                        .arg(size.width()).arg(size.height()),
                    size
                };
            }

            return {true, {}, size};
        }

        QImage matToQImage(const cv::Mat &bgr)
        {
            QImage image(bgr.data, bgr.cols, bgr.rows, static_cast<int>(bgr.step), QImage::Format_BGR888);
            return image.copy();
        }

        std::pair<QImage, double> makeReviewPreview(const cv::Mat &bgr)
        {
            const int longEdge = std::max(bgr.cols, bgr.rows);
            if (longEdge <= kReviewMaxLongEdge)
            {
                return {matToQImage(bgr), 1.0};
            }
            const double scale = static_cast<double>(kReviewMaxLongEdge) / longEdge;
            const int newW = std::max(1, static_cast<int>(std::round(bgr.cols * scale)));
            const int newH = std::max(1, static_cast<int>(std::round(bgr.rows * scale)));
            cv::Mat resized;
            cv::resize(bgr, resized, cv::Size(newW, newH), 0.0, 0.0, cv::INTER_AREA);
            return {matToQImage(resized), scale};
        }

        QVector<QRectF> scaleRects(const QVector<QRectF> &rects, double factor)
        {
            if (factor == 1.0)
            {
                return rects;
            }
            QVector<QRectF> scaled;
            scaled.reserve(rects.size());
            for (const auto &rect: rects)
            {
                scaled.push_back(QRectF(rect.x() * factor,
                                        rect.y() * factor,
                                        rect.width() * factor,
                                        rect.height() * factor));
            }
            return scaled;
        }

        std::string destinationKey(const std::filesystem::path &path)
        {
            auto key = pathToUtf8(path.lexically_normal());
#if defined(_WIN32) || defined(__APPLE__)
            std::ranges::transform(key, key.begin(), [](unsigned char ch)
            {
                return static_cast<char>(std::tolower(ch));
            });
#endif
            return key;
        }

        bool hasDestinationCollisions(const std::vector<ScanResult> &images,
                                      const std::filesystem::path &safeRoot,
                                      QStringList &messages)
        {
            std::unordered_map<std::string, std::filesystem::path> firstSourceForDestination;
            for (const auto &item: images)
            {
                const auto destination = (safeRoot / item.relativePath).lexically_normal();
                const auto key = destinationKey(destination);
                const auto [it, inserted] = firstSourceForDestination.emplace(key, item.sourcePath);
                if (!inserted)
                {
                    messages.push_back(QCoreApplication::translate("redactly::ProcessorWorker",
                                                                   "Output name collision: '%1' and '%2' would both write to '%3'")
                        .arg(pathToQString(it->second),
                             pathToQString(item.sourcePath),
                             pathToQString(destination)));
                    if (messages.size() >= 10)
                    {
                        messages.push_back(QCoreApplication::translate("redactly::ProcessorWorker",
                                                                       "Additional output name collisions omitted."));
                        return true;
                    }
                }
            }
            return !messages.empty();
        }

        std::filesystem::path uniqueTempPath(const std::filesystem::path &destination)
        {
            static thread_local std::mt19937_64 rng{std::random_device{}()};
            std::uniform_int_distribution<std::uint64_t> dist;
            const auto suffix = dist(rng);
            auto tempName = destination.stem();
            tempName += ".redactly-" + std::to_string(suffix);
            tempName += destination.extension();
            const auto parent = destination.parent_path();
            return parent.empty() ? tempName : parent / tempName;
        }

        bool atomicImwrite(const std::filesystem::path &destination, const cv::Mat &image,
                           const std::vector<int> &params = {})
        {
            const auto temp = uniqueTempPath(destination);
            if (!imwriteUnicode(temp, image, params))
            {
                std::error_code ec;
                std::filesystem::remove(temp, ec);
                return false;
            }
            std::error_code ec;
            std::filesystem::rename(temp, destination, ec);
            if (ec)
            {
                std::filesystem::copy_file(temp, destination,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                std::error_code removeEc;
                std::filesystem::remove(temp, removeEc);
                if (ec)
                {
                    return false;
                }
            }
            return true;
        }

        FaceDetections toDetections(const QVector<QRectF> &boxes)
        {
            FaceDetections result;
            result.reserve(boxes.size());
            for (const auto &rect: boxes)
            {
                FaceDetection det;
                det.box = cv::Rect2f(static_cast<float>(rect.x()),
                                     static_cast<float>(rect.y()),
                                     static_cast<float>(rect.width()),
                                     static_cast<float>(rect.height()));
                det.score = 1.0F;
                result.push_back(det);
            }
            return result;
        }

        QVector<QRectF> toQRects(const FaceDetections &faces)
        {
            QVector<QRectF> result;
            result.reserve(static_cast<int>(faces.size()));
            for (const auto &face: faces)
            {
                result.push_back(QRectF(face.box.x, face.box.y, face.box.width, face.box.height));
            }
            return result;
        }
    }

    ProcessorWorker::ProcessorWorker(QString modelPath,
                                     QStringList inputs,
                                     QString outputDirectory,
                                     bool recursive,
                                     float scoreThreshold,
                                     float nmsThreshold,
                                     int mosaicBlockSize,
                                     float paddingRatio,
                                     AnonymizationMethod method,
                                     MaskShape shape,
                                     bool preserveMetadata,
                                     bool reviewEnabled,
                                     QObject *reviewReceiver,
                                     std::shared_ptr<ScrfdFaceDetector> cachedDetector,
                                     bool detectFaces,
                                     bool detectPlates,
                                     QString plateModelPath,
                                     std::shared_ptr<PlateDetector> cachedPlateDetector)
        : modelPath_(std::move(modelPath)),
          inputs_(std::move(inputs)),
          outputDirectory_(std::move(outputDirectory)),
          recursive_(recursive),
          scoreThreshold_(scoreThreshold),
          nmsThreshold_(nmsThreshold),
          mosaicBlockSize_(mosaicBlockSize),
          paddingRatio_(paddingRatio),
          method_(method),
          shape_(shape),
          preserveMetadata_(preserveMetadata),
          reviewEnabled_(reviewEnabled),
          reviewReceiver_(reviewReceiver),
          detectFaces_(detectFaces),
          detectPlates_(detectPlates),
          plateModelPath_(std::move(plateModelPath)),
          detector_(std::move(cachedDetector)),
          plateDetector_(std::move(cachedPlateDetector))
    {
    }

    ProcessorWorker::~ProcessorWorker() = default;

    std::shared_ptr<ScrfdFaceDetector> ProcessorWorker::takeDetector()
    {
        return std::move(detector_);
    }

    std::shared_ptr<PlateDetector> ProcessorWorker::takePlateDetector()
    {
        return std::move(plateDetector_);
    }

    void ProcessorWorker::process()
    {
        cancelled_.store(false, std::memory_order_relaxed);

        try
        {
            if (detectFaces_)
            {
                if (!detector_)
                {
                    emit logMessage(tr("Loading face detection model..."));
                    detector_ = std::make_shared<ScrfdFaceDetector>(modelPath_.toStdString());
                } else
                {
                    emit logMessage(tr("Reusing loaded face detection model."));
                }
            }

            if (detectPlates_)
            {
                if (!plateDetector_)
                {
                    emit logMessage(tr("Loading license plate detection model..."));
                    plateDetector_ = std::make_shared<PlateDetector>(
                        pathToUtf8(pathFromQString(plateModelPath_)));
                } else
                {
                    emit logMessage(tr("Reusing loaded license plate detection model."));
                }
            }

            emit logMessage(tr("Scanning images..."));
            const auto images = scanImages(inputs_, recursive_);
            const int total = static_cast<int>(images.size());
            emit logMessage(tr("Preflight: found %1 supported image(s).").arg(total));
            emit progressChanged(0, total);

            if (cancelled_.load(std::memory_order_acquire))
            {
                emit finished(true);
                return;
            }

            if (total == 0)
            {
                emit logMessage(tr("No supported images were found."));
                emit finished(false);
                return;
            }

            const auto outputRoot = pathFromQString(outputDirectory_);
            std::error_code mkdirError;
            std::filesystem::create_directories(outputRoot, mkdirError);
            if (mkdirError)
            {
                emit logMessage(tr("Cannot create output directory: %1")
                    .arg(QString::fromStdString(mkdirError.message())));
                emit finished(false);
                return;
            }

            std::error_code canonicalError;
            const auto canonicalRoot = std::filesystem::canonical(outputRoot, canonicalError);
            const auto safeRoot = canonicalError
                                      ? outputRoot.lexically_normal()
                                      : canonicalRoot;

            QStringList collisionMessages;
            if (hasDestinationCollisions(images, safeRoot, collisionMessages))
            {
                emit logMessage(tr("Refusing to run because multiple inputs would write to the same output path."));
                for (const auto &message: collisionMessages)
                {
                    emit logMessage(message);
                }
                emit finished(false);
                return;
            }
            emit logMessage(tr("Preflight: output paths are unique."));

            int completed = 0;
            int index = 0;
            int anonymizedCount = 0;
            int copiedCount = 0;
            int skippedCount = 0;
            int failedCount = 0;
            for (const auto &item: images)
            {
                ++index;
                if (cancelled_.load(std::memory_order_acquire))
                {
                    break;
                }

                const auto source = item.sourcePath;
                const auto destination = (safeRoot / item.relativePath).lexically_normal();

                if (!destinationIsSafe(destination, safeRoot))
                {
                    emit logMessage(
                        tr("Skipped unsafe output path for: %1").arg(
                            pathToQString(source.filename())));
                    ++skippedCount;
                    emit progressChanged(++completed, total);
                    continue;
                }

                std::error_code parentMkdirError;
                std::filesystem::create_directories(destination.parent_path(), parentMkdirError);
                if (parentMkdirError)
                {
                    emit logMessage(
                        tr("Skipped (cannot create parent dir): %1 — %2")
                        .arg(pathToQString(source.filename()),
                             QString::fromStdString(parentMkdirError.message())));
                    ++skippedCount;
                    emit progressChanged(++completed, total);
                    continue;
                }

                std::error_code sizeError;
                const auto fileSize = std::filesystem::file_size(source, sizeError);
                if (!sizeError && fileSize > kMaxInputFileBytes)
                {
                    emit logMessage(
                        tr("Skipped (file too large, %1 MB): %2")
                        .arg(static_cast<qulonglong>(fileSize >> 20))
                        .arg(pathToQString(source.filename())));
                    ++skippedCount;
                    emit progressChanged(++completed, total);
                    continue;
                }

                const QString fileName = pathToQString(source.filename());
                emit stageChanged(index, total, tr("Loading"), fileName);

                const auto dimensions = inspectImageDimensions(source);
                if (!dimensions.ok)
                {
                    emit logMessage(tr("Skipped (%1): %2").arg(dimensions.reason, fileName));
                    ++skippedCount;
                    emit progressChanged(++completed, total);
                    continue;
                }

                cv::Mat image = imreadUnicode(source, cv::IMREAD_UNCHANGED);
                if (image.empty())
                {
                    emit logMessage(
                        tr("Skipped unreadable image: %1").arg(pathToQString(source)));
                    ++skippedCount;
                    emit progressChanged(++completed, total);
                    continue;
                }

                applyOrientation(image, readExifOrientation(source));

                if (cancelled_.load(std::memory_order_acquire))
                {
                    break;
                }

                const long long pixelCount =
                        static_cast<long long>(image.cols) * static_cast<long long>(image.rows);
                if (pixelCount > kMaxPixelCount)
                {
                    emit logMessage(
                        tr("Skipped (image too large, %1 × %2): %3")
                        .arg(image.cols).arg(image.rows)
                        .arg(fileName));
                    image.release();
                    ++skippedCount;
                    emit progressChanged(++completed, total);
                    continue;
                }

                const cv::Mat detectMat = toDetectionBgr(image);

                emit stageChanged(index, total, tr("Detecting"), fileName);
                FaceDetections detected;
                if (detector_)
                {
                    detected = detector_->detect(detectMat, scoreThreshold_, nmsThreshold_);
                }
                if (plateDetector_)
                {
                    const auto plates = plateDetector_->detect(detectMat, scoreThreshold_, nmsThreshold_);
                    detected.insert(detected.end(), plates.begin(), plates.end());
                }
                if (cancelled_.load(std::memory_order_acquire))
                {
                    break;
                }
                FaceDetections finalFaces = detected;
                bool doNotSaveThisImage = false;
                bool copyOriginalThisImage = false;

                if (reviewEnabled_ && reviewReceiver_)
                {
                    emit stageChanged(index, total, tr("Reviewing"), fileName);

                    auto [preview, previewScale] = makeReviewPreview(detectMat);
                    const QVector<QRectF> detectedRects = scaleRects(toQRects(detected), previewScale);

                    ReviewResult reviewResult;
                    const bool invoked = QMetaObject::invokeMethod(
                        reviewReceiver_.data(),
                        "requestReview",
                        Qt::BlockingQueuedConnection,
                        Q_RETURN_ARG(redactly::ReviewResult, reviewResult),
                        Q_ARG(QImage, preview),
                        Q_ARG(QString, fileName),
                        Q_ARG(QVector<QRectF>, detectedRects),
                        Q_ARG(int, index),
                        Q_ARG(int, total));

                    if (!invoked)
                    {
                        emit logMessage(tr("Review bridge unavailable; saved without review."));
                    } else
                    {
                        switch (reviewResult.decision)
                        {
                            case ReviewDecision::CancelAll:
                                cancelled_.store(true, std::memory_order_release);
                                break;
                            case ReviewDecision::DoNotSave:
                                doNotSaveThisImage = true;
                                break;
                            case ReviewDecision::CopyOriginal:
                                copyOriginalThisImage = true;
                                break;
                            case ReviewDecision::Save:

                                finalFaces = toDetections(
                                    scaleRects(reviewResult.finalBoxes,
                                               previewScale != 0.0 ? 1.0 / previewScale : 1.0));
                                break;
                        }
                    }
                }

                if (cancelled_.load(std::memory_order_acquire))
                {
                    break;
                }

                if (doNotSaveThisImage)
                {
                    emit logMessage(tr("Skipped without saving: %1").arg(fileName));
                    ++skippedCount;
                    emit progressChanged(++completed, total);
                    continue;
                }

                const auto encodeParams = encodeParamsForExtension(pathToUtf8(destination.extension()));

                if (copyOriginalThisImage)
                {
                    emit stageChanged(index, total, tr("Saving"), fileName);
                    bool copied = false;
                    if (preserveMetadata_)
                    {
                        std::error_code copyError;
                        std::filesystem::copy_file(source, destination,
                                                   std::filesystem::copy_options::overwrite_existing,
                                                   copyError);
                        copied = !copyError;
                    }
                    else
                    {
                        copied = atomicImwrite(destination, image, encodeParams);
                    }

                    if (!copied)
                    {
                        emit logMessage(tr("Failed to copy: %1").arg(
                            pathToQString(destination)));
                        ++failedCount;
                    } else
                    {
                        emit logMessage(tr("Skipped (original copied): %1").arg(fileName));
                        ++copiedCount;
                    }
                    emit progressChanged(++completed, total);
                    continue;
                }

                emit stageChanged(index, total, tr("Applying mosaic"), fileName);
                applyAnonymization(image, finalFaces, method_, mosaicBlockSize_, paddingRatio_, shape_);

                if (cancelled_.load(std::memory_order_acquire))
                {
                    break;
                }

                emit stageChanged(index, total, tr("Saving"), fileName);
                if (!atomicImwrite(destination, image, encodeParams))
                {
                    emit logMessage(tr("Failed to save: %1").arg(pathToQString(destination)));
                    ++failedCount;
                } else
                {
                    if (preserveMetadata_ && !copyMetadata(source, destination, true))
                    {
                        emit logMessage(tr("Saved, but could not copy metadata: %1").arg(fileName));
                    }
                    emit logMessage(tr("Processed %1 face(s): %2")
                        .arg(static_cast<int>(finalFaces.size()))
                        .arg(fileName));
                    ++anonymizedCount;
                }

                emit progressChanged(++completed, total);
            }

            emit logMessage(
                tr("Summary: %1 anonymized, %2 copied, %3 skipped, %4 failed (of %5).")
                    .arg(anonymizedCount).arg(copiedCount).arg(skippedCount).arg(failedCount).arg(total));

            if (cancelled_.load(std::memory_order_acquire))
            {
                emit finished(true);
                return;
            }

            emit logMessage(tr("Done."));
            emit finished(false);
        } catch (const std::exception &exception)
        {
            emit logMessage(tr("Error: %1").arg(exception.what()));
            emit finished(cancelled_.load(std::memory_order_acquire));
        }
    }

    void ProcessorWorker::cancel()
    {
        cancelled_.store(true, std::memory_order_release);
    }
}
