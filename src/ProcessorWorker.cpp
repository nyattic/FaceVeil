#include "faceveil/ProcessorWorker.hpp"

#include "faceveil/ImageScanner.hpp"
#include "faceveil/Mosaic.hpp"
#include "faceveil/ReviewTypes.hpp"
#include "faceveil/ScrfdFaceDetector.hpp"

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

namespace faceveil
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
            QImageReader reader(QString::fromStdString(source.string()));
            reader.setAutoTransform(false);

            const QSize size = reader.size();
            if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
            {
                return {false, "cannot inspect image dimensions", {}};
            }

            const long long pixelCount =
                    static_cast<long long>(size.width()) * static_cast<long long>(size.height());
            if (pixelCount > kMaxPixelCount)
            {
                return {
                    false,
                    QString("image too large, %1 x %2").arg(size.width()).arg(size.height()),
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

        bool isWithinRoot(const std::filesystem::path &candidate, const std::filesystem::path &root)
        {
            std::error_code ec;
            const auto relative = std::filesystem::relative(candidate, root, ec);
            if (ec || relative.empty())
            {
                return false;
            }
            const auto first = relative.begin();
            return first != relative.end() && first->string() != "..";
        }

        bool destinationIsSafe(const std::filesystem::path &destination,
                               const std::filesystem::path &safeRoot)
        {
            std::error_code ec;
            auto current = destination;
            while (!current.empty() && current != current.root_path())
            {
                if (std::filesystem::exists(current, ec))
                {
                    if (std::filesystem::is_symlink(current, ec))
                    {
                        auto resolved = std::filesystem::canonical(current, ec);
                        if (ec || !isWithinRoot(resolved, safeRoot))
                        {
                            return false;
                        }
                    }
                    break;
                }
                current = current.parent_path();
            }

            if (std::filesystem::exists(destination, ec))
            {
                auto resolved = std::filesystem::canonical(destination, ec);
                if (ec || !isWithinRoot(resolved, safeRoot))
                {
                    return false;
                }
            }

            const auto lexical = destination.lexically_normal();
            return isWithinRoot(lexical, safeRoot) || lexical == safeRoot.lexically_normal();
        }

        std::string destinationKey(const std::filesystem::path &path)
        {
            auto key = path.lexically_normal().string();
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
                    messages.push_back(QString("Output name collision: '%1' and '%2' would both write to '%3'")
                        .arg(QString::fromStdString(it->second.string()),
                             QString::fromStdString(item.sourcePath.string()),
                             QString::fromStdString(destination.string())));
                    if (messages.size() >= 10)
                    {
                        messages.push_back("Additional output name collisions omitted.");
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
            const auto stem = destination.stem().string();
            const auto ext = destination.extension().string();
            const auto parent = destination.parent_path();
            const auto tempName = stem + ".faceveil-" + std::to_string(suffix) + ext;
            return parent.empty() ? std::filesystem::path(tempName) : parent / tempName;
        }

        bool atomicImwrite(const std::filesystem::path &destination, const cv::Mat &image)
        {
            const auto temp = uniqueTempPath(destination);
            if (!cv::imwrite(temp.string(), image))
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
                                     bool reviewEnabled,
                                     QObject *reviewReceiver,
                                     std::shared_ptr<ScrfdFaceDetector> cachedDetector)
        : modelPath_(std::move(modelPath)),
          inputs_(std::move(inputs)),
          outputDirectory_(std::move(outputDirectory)),
          recursive_(recursive),
          scoreThreshold_(scoreThreshold),
          nmsThreshold_(nmsThreshold),
          mosaicBlockSize_(mosaicBlockSize),
          paddingRatio_(paddingRatio),
          method_(method),
          reviewEnabled_(reviewEnabled),
          reviewReceiver_(reviewReceiver),
          detector_(std::move(cachedDetector))
    {
    }

    ProcessorWorker::~ProcessorWorker() = default;

    std::shared_ptr<ScrfdFaceDetector> ProcessorWorker::takeDetector()
    {
        return std::move(detector_);
    }

    void ProcessorWorker::process()
    {
        cancelled_.store(false, std::memory_order_relaxed);

        try
        {
            if (!detector_)
            {
                emit logMessage("Loading SCRFD model...");
                detector_ = std::make_shared<ScrfdFaceDetector>(modelPath_.toStdString());
            } else
            {
                emit logMessage("Reusing loaded SCRFD model.");
            }

            emit logMessage("Scanning images...");
            const auto images = scanImages(inputs_, recursive_);
            const int total = static_cast<int>(images.size());
            emit logMessage(QString("Preflight: found %1 supported image(s).").arg(total));
            emit progressChanged(0, total);

            if (cancelled_.load(std::memory_order_acquire))
            {
                emit finished(true);
                return;
            }

            if (total == 0)
            {
                emit logMessage("No supported images were found.");
                emit finished(false);
                return;
            }

            const auto outputRoot = std::filesystem::path(outputDirectory_.toStdString());
            std::error_code mkdirError;
            std::filesystem::create_directories(outputRoot, mkdirError);
            if (mkdirError)
            {
                emit logMessage(QString("Cannot create output directory: %1")
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
                emit logMessage("Refusing to run because multiple inputs would write to the same output path.");
                for (const auto &message: collisionMessages)
                {
                    emit logMessage(message);
                }
                emit finished(false);
                return;
            }
            emit logMessage("Preflight: output paths are unique.");

            int completed = 0;
            int index = 0;
            for (const auto &item: images)
            {
                ++index;
                if (cancelled_.load(std::memory_order_acquire))
                {
                    emit finished(true);
                    return;
                }

                const auto source = item.sourcePath;
                const auto destination = (safeRoot / item.relativePath).lexically_normal();

                if (!destinationIsSafe(destination, safeRoot))
                {
                    emit logMessage(
                        QString("Skipped unsafe output path for: %1").arg(
                            QString::fromStdString(source.filename().string())));
                    emit progressChanged(++completed, total);
                    continue;
                }

                std::error_code parentMkdirError;
                std::filesystem::create_directories(destination.parent_path(), parentMkdirError);
                if (parentMkdirError)
                {
                    emit logMessage(
                        QString("Skipped (cannot create parent dir): %1 — %2")
                        .arg(QString::fromStdString(source.filename().string()),
                             QString::fromStdString(parentMkdirError.message())));
                    emit progressChanged(++completed, total);
                    continue;
                }

                std::error_code sizeError;
                const auto fileSize = std::filesystem::file_size(source, sizeError);
                if (!sizeError && fileSize > kMaxInputFileBytes)
                {
                    emit logMessage(
                        QString("Skipped (file too large, %1 MB): %2")
                        .arg(static_cast<qulonglong>(fileSize >> 20))
                        .arg(QString::fromStdString(source.filename().string())));
                    emit progressChanged(++completed, total);
                    continue;
                }

                const QString fileName = QString::fromStdString(source.filename().string());
                emit stageChanged(index, total, "Loading", fileName);

                const auto dimensions = inspectImageDimensions(source);
                if (!dimensions.ok)
                {
                    emit logMessage(QString("Skipped (%1): %2").arg(dimensions.reason, fileName));
                    emit progressChanged(++completed, total);
                    continue;
                }

                cv::Mat image = cv::imread(source.string(), cv::IMREAD_COLOR);
                if (image.empty())
                {
                    emit logMessage(
                        QString("Skipped unreadable image: %1").arg(QString::fromStdString(source.string())));
                    emit progressChanged(++completed, total);
                    continue;
                }

                if (cancelled_.load(std::memory_order_acquire))
                {
                    emit finished(true);
                    return;
                }

                const long long pixelCount =
                        static_cast<long long>(image.cols) * static_cast<long long>(image.rows);
                if (pixelCount > kMaxPixelCount)
                {
                    emit logMessage(
                        QString("Skipped (image too large, %1 × %2): %3")
                        .arg(image.cols).arg(image.rows)
                        .arg(fileName));
                    image.release();
                    emit progressChanged(++completed, total);
                    continue;
                }

                emit stageChanged(index, total, "Detecting", fileName);
                const auto detected = detector_->detect(image, scoreThreshold_, nmsThreshold_);
                if (cancelled_.load(std::memory_order_acquire))
                {
                    emit finished(true);
                    return;
                }
                FaceDetections finalFaces = detected;
                bool doNotSaveThisImage = false;
                bool copyOriginalThisImage = false;

                if (reviewEnabled_ && reviewReceiver_)
                {
                    emit stageChanged(index, total, "Reviewing", fileName);

                    auto [preview, previewScale] = makeReviewPreview(image);
                    const QVector<QRectF> detectedRects = scaleRects(toQRects(detected), previewScale);

                    ReviewResult reviewResult;
                    const bool invoked = QMetaObject::invokeMethod(
                        reviewReceiver_.data(),
                        "requestReview",
                        Qt::BlockingQueuedConnection,
                        Q_RETURN_ARG(faceveil::ReviewResult, reviewResult),
                        Q_ARG(QImage, preview),
                        Q_ARG(QString, fileName),
                        Q_ARG(QVector<QRectF>, detectedRects),
                        Q_ARG(int, index),
                        Q_ARG(int, total));

                    if (!invoked)
                    {
                        emit logMessage("Review bridge unavailable; saved without review.");
                    } else
                    {
                        switch (reviewResult.decision)
                        {
                            case ReviewDecision::CancelAll:
                                cancelled_.store(true, std::memory_order_release);
                                emit finished(true);
                                return;
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
                    emit finished(true);
                    return;
                }

                if (doNotSaveThisImage)
                {
                    emit logMessage(QString("Skipped without saving: %1").arg(fileName));
                    emit progressChanged(++completed, total);
                    continue;
                }

                if (copyOriginalThisImage)
                {
                    emit stageChanged(index, total, "Saving", fileName);
                    if (!atomicImwrite(destination, image))
                    {
                        emit logMessage(QString("Failed to copy: %1").arg(
                            QString::fromStdString(destination.string())));
                    } else
                    {
                        emit logMessage(QString("Skipped (original copied): %1").arg(fileName));
                    }
                    emit progressChanged(++completed, total);
                    continue;
                }

                emit stageChanged(index, total, "Applying mosaic", fileName);
                applyAnonymization(image, finalFaces, method_, mosaicBlockSize_, paddingRatio_);

                if (cancelled_.load(std::memory_order_acquire))
                {
                    emit finished(true);
                    return;
                }

                emit stageChanged(index, total, "Saving", fileName);
                if (!atomicImwrite(destination, image))
                {
                    emit logMessage(QString("Failed to save: %1").arg(QString::fromStdString(destination.string())));
                } else
                {
                    emit logMessage(QString("Processed %1 face(s): %2")
                        .arg(static_cast<int>(finalFaces.size()))
                        .arg(fileName));
                }

                emit progressChanged(++completed, total);
            }

            emit logMessage("Done.");
            emit finished(false);
        } catch (const std::exception &exception)
        {
            emit logMessage(QString("Error: %1").arg(exception.what()));
            emit finished(cancelled_.load(std::memory_order_acquire));
        }
    }

    void ProcessorWorker::cancel()
    {
        cancelled_.store(true, std::memory_order_release);
    }
}
