#include "redactly/ProcessorWorker.hpp"

#include "redactly/ImageIo.hpp"
#include "redactly/ImageScanner.hpp"
#include "redactly/OrderedParallel.hpp"
#include "redactly/OutputPlan.hpp"
#include "redactly/PathSafety.hpp"
#include "redactly/PathUtil.hpp"
#include "redactly/PlateDetector.hpp"
#include "redactly/Mosaic.hpp"
#include "redactly/ReviewTypes.hpp"
#include "redactly/YunetFaceDetector.hpp"
#include "redactly/VideoIo.hpp"
#include "redactly/VideoProcessor.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImageReader>
#include <QMetaObject>
#include <QRectF>
#include <QSize>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <random>
#include <system_error>
#include <thread>

namespace redactly
{
    namespace
    {
        constexpr std::uintmax_t kMaxInputFileBytes = 1ULL << 30;
        constexpr long long kMaxPixelCount = 200LL * 1000LL * 1000LL;
        constexpr int kVideoDetectionInputSize = 960;

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
            std::error_code existsError;
            if (std::filesystem::exists(destination, existsError) || existsError)
            {
                return false;
            }
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
                                           std::filesystem::copy_options::none, ec);
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

    struct ProcessorWorker::ItemOutcome
    {
        QStringList logs;
        int redacted = 0;
        int copied = 0;
        int skipped = 0;
        int failed = 0;
        int unredacted = 0;
        bool cancelled = false;
    };

    ProcessorWorker::ProcessorWorker(ProcessingRequest request, DetectorCache cache)
        : modelPath_(std::move(request.modelPath)),
          inputs_(std::move(request.inputs)),
          outputDirectory_(std::move(request.outputDirectory)),
          recursive_(request.recursive),
          scoreThreshold_(request.scoreThreshold),
          nmsThreshold_(request.nmsThreshold),
          mosaicBlockSize_(request.mosaicBlockSize),
          paddingRatio_(request.paddingRatio),
          method_(request.method),
          shape_(request.shape),
          softEdges_(request.softEdges),
          preserveMetadata_(request.preserveMetadata),
          reviewEnabled_(request.reviewEnabled),
          reviewReceiver_(request.reviewReceiver),
          detectFaces_(request.detectFaces),
          detectPlates_(request.detectPlates),
          plateModelPath_(std::move(request.plateModelPath)),
          gpuAcceleration_(request.gpuAcceleration),
          videoCrf_(request.videoCrf),
          detector_(std::move(cache.face)),
          plateDetector_(std::move(cache.plate)),
          videoDetector_(std::move(cache.videoFace))
    {
    }

    ProcessorWorker::~ProcessorWorker() = default;

    std::shared_ptr<YunetFaceDetector> ProcessorWorker::takeDetector()
    {
        return std::move(detector_);
    }

    std::shared_ptr<PlateDetector> ProcessorWorker::takePlateDetector()
    {
        return std::move(plateDetector_);
    }

    std::shared_ptr<YunetFaceDetector> ProcessorWorker::takeVideoDetector()
    {
        return std::move(videoDetector_);
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
                    try
                    {
                        detector_ = std::make_shared<YunetFaceDetector>(modelPath_.toStdString(),
                                                                        640, gpuAcceleration_);
                        if (detector_->accelerator() != OrtAccelerator::None)
                        {
                            const cv::Mat warmupFrame(640, 640, CV_8UC3, cv::Scalar(0, 0, 0));
                            detector_->detect(warmupFrame, scoreThreshold_, nmsThreshold_);
                        }
                    }
                    catch (const Ort::Exception &)
                    {
                        emit logMessage(tr("GPU acceleration can't run the face model; "
                                           "using the CPU instead."));
                        detector_ = std::make_shared<YunetFaceDetector>(modelPath_.toStdString(),
                                                                        640, false);
                    }
                } else
                {
                    emit logMessage(tr("Reusing loaded face detection model."));
                }
                emit logMessage(tr("Face detection backend: %1")
                                    .arg(QString::fromLatin1(ortAcceleratorName(detector_->accelerator()))));
            }

            if (detectPlates_)
            {
                if (!plateDetector_)
                {
                    emit logMessage(tr("Loading license plate detection model..."));
                    try
                    {
                        plateDetector_ = std::make_shared<PlateDetector>(
                            pathToUtf8(pathFromQString(plateModelPath_)), gpuAcceleration_);
                        if (plateDetector_->accelerator() != OrtAccelerator::None)
                        {
                            const cv::Mat warmupFrame(512, 512, CV_8UC3, cv::Scalar(0, 0, 0));
                            plateDetector_->detect(warmupFrame, scoreThreshold_, nmsThreshold_);
                        }
                    }
                    catch (const Ort::Exception &)
                    {
                        emit logMessage(tr("GPU acceleration can't run the license plate model; "
                                           "using the CPU instead."));
                        plateDetector_ = std::make_shared<PlateDetector>(
                            pathToUtf8(pathFromQString(plateModelPath_)), false);
                    }
                } else
                {
                    emit logMessage(tr("Reusing loaded license plate detection model."));
                }
                emit logMessage(tr("License plate detection backend: %1")
                                    .arg(QString::fromLatin1(ortAcceleratorName(plateDetector_->accelerator()))));
            }

            emit logMessage(tr("Scanning inputs..."));
            const auto images = scanMedia(inputs_, recursive_, true);
            const int total = static_cast<int>(images.size());
            emit logMessage(tr("Preflight: found %n supported file(s).", nullptr, total));
            emit progressChanged(0, total);

            if (cancelled_.load(std::memory_order_acquire))
            {
                emit finished(RunOutcome::Cancelled);
                return;
            }

            if (total == 0)
            {
                emit logMessage(tr("No supported files were found."));
                emit finished(RunOutcome::Failed);
                return;
            }

            const auto outputRoot = pathFromQString(outputDirectory_);
            std::error_code mkdirError;
            std::filesystem::create_directories(outputRoot, mkdirError);
            if (mkdirError)
            {
                emit logMessage(tr("Cannot create output directory: %1")
                    .arg(QString::fromStdString(mkdirError.message())));
                emit finished(RunOutcome::Failed);
                return;
            }

            std::error_code canonicalError;
            const auto canonicalRoot = std::filesystem::canonical(outputRoot, canonicalError);
            const auto safeRoot = canonicalError
                                      ? outputRoot.lexically_normal()
                                      : canonicalRoot;

            const auto outputConflicts = findOutputConflicts(images, safeRoot);
            if (!outputConflicts.empty())
            {
                emit logMessage(tr("Refusing to run because an output path is already in use."));
                for (const auto &conflict: outputConflicts)
                {
                    if (conflict.kind == OutputConflict::Kind::DuplicateDestination)
                    {
                        emit logMessage(tr("Output name collision: '%1' and '%2' would both write to '%3'")
                            .arg(pathToQString(conflict.otherSource),
                                 pathToQString(conflict.source),
                                 pathToQString(conflict.destination)));
                    }
                    else
                    {
                        emit logMessage(tr("Existing output would be overwritten: '%1'")
                            .arg(pathToQString(conflict.destination)));
                    }
                }
                if (outputConflicts.size() >= 10)
                    emit logMessage(tr("Additional output conflicts omitted."));
                emit finished(RunOutcome::Failed);
                return;
            }
            emit logMessage(tr("Preflight: output paths are available."));

            int completed = 0;
            int redactedCount = 0;
            int copiedCount = 0;
            int skippedCount = 0;
            int failedCount = 0;
            int unredactedCount = 0;

            const auto applyOutcome = [&](ItemOutcome &&outcome)
            {
                for (const auto &message: outcome.logs)
                {
                    emit logMessage(message);
                }
                redactedCount += outcome.redacted;
                copiedCount += outcome.copied;
                skippedCount += outcome.skipped;
                failedCount += outcome.failed;
                unredactedCount += outcome.unredacted;
                if (!outcome.cancelled)
                {
                    emit progressChanged(++completed, total);
                }
            };

            if (reviewEnabled_ && reviewReceiver_)
            {
                int index = 0;
                for (const auto &item: images)
                {
                    ++index;
                    if (cancelled_.load(std::memory_order_acquire))
                    {
                        break;
                    }
                    auto outcome = processItem(item, safeRoot, index, total, true);
                    const bool stop = outcome.cancelled;
                    applyOutcome(std::move(outcome));
                    if (stop)
                    {
                        break;
                    }
                }
            }
            else
            {
                std::vector<std::size_t> imageIndexes;
                std::vector<std::size_t> videoIndexes;
                for (std::size_t i = 0; i < images.size(); ++i)
                {
                    (isSupportedVideo(images[i].sourcePath) ? videoIndexes : imageIndexes)
                            .push_back(i);
                }

                const unsigned threadCount =
                        std::min(4U, std::max(1U, std::thread::hardware_concurrency()));
                processOrdered<ItemOutcome>(
                    imageIndexes.size(), threadCount, threadCount, cancelled_,
                    [&](std::size_t k)
                    {
                        const auto i = imageIndexes[k];
                        return processItem(images[i], safeRoot,
                                           static_cast<int>(i) + 1, total, false);
                    },
                    [&](std::size_t, ItemOutcome &&outcome)
                    {
                        applyOutcome(std::move(outcome));
                    });

                for (const auto i: videoIndexes)
                {
                    if (cancelled_.load(std::memory_order_acquire))
                    {
                        break;
                    }
                    auto outcome = processItem(images[i], safeRoot,
                                               static_cast<int>(i) + 1, total, false);
                    const bool stop = outcome.cancelled;
                    applyOutcome(std::move(outcome));
                    if (stop)
                    {
                        break;
                    }
                }
            }

            emit logMessage(
                tr("Summary: %1 redacted, %2 saved without redaction, %3 copied, %4 skipped, %5 failed (of %6).")
                    .arg(redactedCount).arg(unredactedCount).arg(copiedCount).arg(skippedCount)
                    .arg(failedCount).arg(total));

            RunSummary summary;
            summary.total = total;
            summary.redacted = redactedCount;
            summary.copied = copiedCount;
            summary.skipped = skippedCount;
            summary.failed = failedCount;
            summary.unredacted = unredactedCount;
            emit summaryAvailable(summary);

            if (unredactedCount > 0)
            {
                emit logMessage(
                    tr("Warning: %n image(s) were saved with no regions redacted. Check them before sharing.",
                       nullptr, unredactedCount));
            }

            if (cancelled_.load(std::memory_order_acquire))
            {
                emit finished(RunOutcome::Cancelled);
                return;
            }

            if (failedCount > 0 || skippedCount > 0 || unredactedCount > 0)
            {
                emit logMessage(tr("Completed with warnings. Review the summary before sharing."));
                emit finished(RunOutcome::CompletedWithWarnings);
            }
            else
            {
                emit logMessage(tr("Done."));
                emit finished(RunOutcome::Completed);
            }
        } catch (const std::exception &exception)
        {
            emit logMessage(tr("Error: %1").arg(exception.what()));
            emit finished(cancelled_.load(std::memory_order_acquire)
                              ? RunOutcome::Cancelled
                              : RunOutcome::Failed);
        } catch (...)
        {
            emit logMessage(tr("Unexpected error — processing stopped."));
            emit finished(cancelled_.load(std::memory_order_acquire)
                              ? RunOutcome::Cancelled
                              : RunOutcome::Failed);
        }
    }

    ProcessorWorker::ItemOutcome ProcessorWorker::processItem(const ScanResult &item,
                                                              const std::filesystem::path &safeRoot,
                                                              const int index,
                                                              const int total,
                                                              const bool allowReview)
    {
        ItemOutcome outcome;
        try
        {
            const auto source = item.sourcePath;
            const auto destination = (safeRoot / outputRelativePath(item)).lexically_normal();

            if (!destinationIsSafe(destination, safeRoot))
            {
                outcome.logs.push_back(
                    tr("Skipped unsafe output path for: %1").arg(pathToQString(source.filename())));
                outcome.skipped = 1;
                return outcome;
            }

            std::error_code sameFileError;
            if (std::filesystem::exists(destination, sameFileError) &&
                std::filesystem::equivalent(source, destination, sameFileError))
            {
                outcome.logs.push_back(
                    tr("Skipped (source and destination are the same file): %1")
                    .arg(pathToQString(source.filename())));
                outcome.skipped = 1;
                return outcome;
            }

            std::error_code parentMkdirError;
            std::filesystem::create_directories(destination.parent_path(), parentMkdirError);
            if (parentMkdirError)
            {
                outcome.logs.push_back(
                    tr("Skipped (cannot create parent dir): %1 — %2")
                    .arg(pathToQString(source.filename()),
                         QString::fromStdString(parentMkdirError.message())));
                outcome.skipped = 1;
                return outcome;
            }

            if (isSupportedVideo(source))
            {
                return processVideoItem(item, destination, index, total);
            }

            std::error_code sizeError;
            const auto fileSize = std::filesystem::file_size(source, sizeError);
            if (!sizeError && fileSize > kMaxInputFileBytes)
            {
                outcome.logs.push_back(
                    tr("Skipped (file too large, %1 MB): %2")
                    .arg(static_cast<qulonglong>(fileSize >> 20))
                    .arg(pathToQString(source.filename())));
                outcome.skipped = 1;
                return outcome;
            }

            const QString fileName = pathToQString(source.filename());
            emit stageChanged(index, total, tr("Loading"), fileName);

            const auto dimensions = inspectImageDimensions(source);
            if (!dimensions.ok)
            {
                outcome.logs.push_back(tr("Skipped (%1): %2").arg(dimensions.reason, fileName));
                outcome.skipped = 1;
                return outcome;
            }

            cv::Mat image = imreadUnicode(source, cv::IMREAD_UNCHANGED);
            if (image.empty())
            {
                outcome.logs.push_back(
                    tr("Skipped unreadable image: %1").arg(pathToQString(source)));
                outcome.skipped = 1;
                return outcome;
            }

            applyOrientation(image, readExifOrientation(source));

            if (cancelled_.load(std::memory_order_acquire))
            {
                outcome.cancelled = true;
                return outcome;
            }

            const long long pixelCount =
                    static_cast<long long>(image.cols) * static_cast<long long>(image.rows);
            if (pixelCount > kMaxPixelCount)
            {
                outcome.logs.push_back(
                    tr("Skipped (image too large, %1 × %2): %3")
                    .arg(image.cols).arg(image.rows)
                    .arg(fileName));
                outcome.skipped = 1;
                return outcome;
            }

            const cv::Mat detectMat = toDetectionBgr(image);

            emit stageChanged(index, total, tr("Detecting"), fileName);
            FaceDetections detected;
            {
                std::lock_guard lock(detectMutex_);
                if (detector_)
                {
                    detected = detector_->detect(detectMat, scoreThreshold_, nmsThreshold_);
                }
                if (plateDetector_)
                {
                    const auto plates = plateDetector_->detect(detectMat, scoreThreshold_, nmsThreshold_);
                    detected.insert(detected.end(), plates.begin(), plates.end());
                }
            }
            if (cancelled_.load(std::memory_order_acquire))
            {
                outcome.cancelled = true;
                return outcome;
            }
            FaceDetections finalFaces = detected;
            bool doNotSaveThisImage = false;
            bool copyOriginalThisImage = false;

            if (allowReview && reviewEnabled_ && reviewReceiver_)
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
                    Q_ARG(int, total),
                    Q_ARG(double, previewScale));

                if (!invoked)
                {
                    outcome.logs.push_back(tr("Review bridge unavailable; saved without review."));
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
                outcome.cancelled = true;
                return outcome;
            }

            if (doNotSaveThisImage)
            {
                outcome.logs.push_back(tr("Skipped without saving: %1").arg(fileName));
                outcome.skipped = 1;
                return outcome;
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
                                               std::filesystem::copy_options::none,
                                               copyError);
                    copied = !copyError;
                }
                else
                {
                    copied = atomicImwrite(destination, image, encodeParams);
                }

                if (!copied)
                {
                    outcome.logs.push_back(tr("Failed to copy: %1").arg(
                        pathToQString(destination)));
                    outcome.failed = 1;
                } else
                {
                    outcome.logs.push_back(tr("Skipped (original copied): %1").arg(fileName));
                    outcome.copied = 1;
                }
                return outcome;
            }

            emit stageChanged(index, total, tr("Applying mosaic"), fileName);
            applyAnonymization(image, finalFaces, method_, mosaicBlockSize_, paddingRatio_,
                               shape_, softEdges_);

            if (cancelled_.load(std::memory_order_acquire))
            {
                outcome.cancelled = true;
                return outcome;
            }

            emit stageChanged(index, total, tr("Saving"), fileName);
            if (!atomicImwrite(destination, image, encodeParams))
            {
                outcome.logs.push_back(tr("Failed to save: %1").arg(pathToQString(destination)));
                outcome.failed = 1;
            } else
            {
                if (preserveMetadata_ && !copyMetadata(source, destination, true))
                {
                    outcome.logs.push_back(tr("Saved, but could not copy metadata: %1").arg(fileName));
                }
                if (finalFaces.empty())
                {
                    outcome.logs.push_back(tr("Saved with no regions redacted: %1").arg(fileName));
                    outcome.unredacted = 1;
                } else
                {
                    outcome.logs.push_back(
                        tr("Redacted %n region(s): %1", nullptr,
                           static_cast<int>(finalFaces.size()))
                            .arg(fileName));
                    outcome.redacted = 1;
                }
            }
        } catch (const std::exception &exception)
        {
            outcome.logs.push_back(tr("Error processing %1: %2")
                .arg(pathToQString(item.sourcePath.filename()),
                     QString::fromUtf8(exception.what())));
            outcome.failed = 1;
        } catch (...)
        {
            outcome.logs.push_back(tr("Error processing %1")
                .arg(pathToQString(item.sourcePath.filename())));
            outcome.failed = 1;
        }
        return outcome;
    }

    ProcessorWorker::ItemOutcome ProcessorWorker::processVideoItem(const ScanResult &item,
                                                                   const std::filesystem::path &destination,
                                                                   const int index,
                                                                   const int total)
    {
        ItemOutcome outcome;
        const QString fileName = pathToQString(item.sourcePath.filename());

        if (reviewEnabled_)
        {
            outcome.logs.push_back(tr("Videos are processed without review: %1").arg(fileName));
        }
        if (preserveMetadata_)
        {
            outcome.logs.push_back(
                tr("Metadata preservation is not available for videos; metadata was removed: %1")
                    .arg(fileName));
        }

        QString toolsError;
        const auto tools = locateFfmpegTools(&toolsError);
        if (!tools)
        {
            outcome.logs.push_back(tr("Failed (%1): %2").arg(toolsError, fileName));
            outcome.failed = 1;
            return outcome;
        }

        emit stageChanged(index, total, tr("Inspecting"), fileName);
        QString probeError;
        const auto info = probeVideo(*tools, pathToQString(item.sourcePath), &probeError);
        if (!info)
        {
            outcome.logs.push_back(tr("Failed (%1): %2").arg(probeError, fileName));
            outcome.failed = 1;
            return outcome;
        }
        const auto unsupported = videoUnsupportedReason(*info);
        if (!unsupported.isEmpty())
        {
            outcome.logs.push_back(
                tr("Failed (unsupported video: %1): %2").arg(unsupported, fileName));
            outcome.failed = 1;
            return outcome;
        }
        if (info->isVfr)
        {
            outcome.logs.push_back(
                tr("Note: variable frame rate is converted to a constant frame rate: %1")
                    .arg(fileName));
        }

        VideoProcessOptions options;
        options.scoreThreshold = scoreThreshold_;
        options.nmsThreshold = nmsThreshold_;
        options.mosaicBlockSize = mosaicBlockSize_;
        options.paddingRatio = paddingRatio_;
        options.method = method_;
        options.shape = shape_;
        options.softEdges = softEdges_;
        options.crf = videoCrf_;
        options.hardwareEncoder = gpuAcceleration_;

        if (detector_ && !videoDetector_)
        {
            emit logMessage(tr("Loading face detection model for video..."));
            try
            {
                videoDetector_ = std::make_shared<YunetFaceDetector>(
                    modelPath_.toStdString(), kVideoDetectionInputSize, gpuAcceleration_);
                if (videoDetector_->accelerator() != OrtAccelerator::None)
                {
                    const cv::Mat warmupFrame(kVideoDetectionInputSize, kVideoDetectionInputSize,
                                              CV_8UC3, cv::Scalar(0, 0, 0));
                    videoDetector_->detect(warmupFrame, scoreThreshold_, nmsThreshold_);
                }
            }
            catch (const Ort::Exception &)
            {
                emit logMessage(tr("GPU acceleration can't run the video face model at %1 px; "
                                   "using the CPU instead.")
                                    .arg(kVideoDetectionInputSize));
                videoDetector_ = std::make_shared<YunetFaceDetector>(
                    modelPath_.toStdString(), kVideoDetectionInputSize, false);
            }
            emit logMessage(tr("Video face detection: %1 px · %2")
                                .arg(videoDetector_->inputSize())
                                .arg(QString::fromLatin1(
                                    ortAcceleratorName(videoDetector_->accelerator()))));
        }

        const float detectionThreshold =
                std::min(options.tracker.lowScoreThreshold, scoreThreshold_);
        const auto detect = [this, detectionThreshold](const cv::Mat &frame)
        {
            std::lock_guard lock(detectMutex_);
            FaceDetections detections;
            if (videoDetector_)
            {
                detections = videoDetector_->detect(frame, detectionThreshold, nmsThreshold_);
            }
            if (plateDetector_)
            {
                const auto plates =
                        plateDetector_->detect(frame, detectionThreshold, nmsThreshold_);
                detections.insert(detections.end(), plates.begin(), plates.end());
            }
            return detections;
        };

        QElapsedTimer passTimer;
        QElapsedTimer totalTimer;
        totalTimer.start();
        int lastPass = 0;
        int lastPercent = -1;
        const auto progress = [&](int pass, qint64 frame, qint64 totalFrames)
        {
            if (pass != lastPass)
            {
                passTimer.start();
                lastPass = pass;
                lastPercent = -1;
            }
            const int percent = totalFrames > 0
                                    ? static_cast<int>(std::min<qint64>(100, frame * 100 / totalFrames))
                                    : 0;
            if (percent == lastPercent)
            {
                return;
            }
            lastPercent = percent;
            QString stage = pass == 1 ? tr("Analyzing %1%").arg(percent)
                                      : tr("Encoding %1%").arg(percent);
            const qint64 elapsedMs = passTimer.elapsed();
            if (frame > 0 && elapsedMs > 2000 && totalFrames > frame)
            {
                const qint64 remaining = elapsedMs * (totalFrames - frame) / frame / 1000;
                stage += QStringLiteral("  ·  ");
                stage += remaining >= 60
                             ? tr("%1m %2s left").arg(remaining / 60).arg(remaining % 60)
                             : tr("%1s left").arg(remaining);
            }
            emit stageChanged(index, total, stage, fileName);
        };

        const auto result = processVideo(*tools,
                                         pathToQString(item.sourcePath),
                                         pathToQString(destination),
                                         *info, options, detect,
                                         cancelled_, progress);
        switch (result.status)
        {
            case VideoProcessStatus::Completed:
                if (result.trackCount == 0)
                {
                    outcome.logs.push_back(
                        tr("Saved with no regions redacted: %1").arg(fileName));
                    outcome.unredacted = 1;
                }
                else
                {
                    outcome.logs.push_back(
                        tr("Redacted %n region(s): %1", nullptr, result.trackCount)
                            .arg(fileName));
                    outcome.redacted = 1;
                }
                if (const double elapsedSeconds = totalTimer.elapsed() / 1000.0;
                    elapsedSeconds > 0 && info->fps() > 0)
                {
                    const double videoSeconds = static_cast<double>(result.frameCount) / info->fps();
                    outcome.logs.push_back(
                        tr("Processed %1 frames in %2s (%3× real time): %4")
                            .arg(result.frameCount)
                            .arg(elapsedSeconds, 0, 'f', 1)
                            .arg(videoSeconds / elapsedSeconds, 0, 'f', 1)
                            .arg(fileName));
                }
                if (!result.encoderName.isEmpty())
                {
                    outcome.logs.push_back(
                        tr("Video encoder: %1").arg(result.encoderName));
                }
                break;
            case VideoProcessStatus::Cancelled:
                outcome.cancelled = true;
                break;
            case VideoProcessStatus::Failed:
                outcome.logs.push_back(
                    tr("Failed to process video %1: %2").arg(fileName, result.error));
                outcome.failed = 1;
                break;
        }
        return outcome;
    }

    void ProcessorWorker::cancel()
    {
        cancelled_.store(true, std::memory_order_release);
    }
}
