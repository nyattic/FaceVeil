#include "cloakframe/DetectionGeometry.hpp"
#include "cloakframe/ImageIo.hpp"
#include "cloakframe/ImageScanner.hpp"
#include "cloakframe/Mosaic.hpp"
#include "cloakframe/ModelCatalog.hpp"
#include "cloakframe/OnnxGraphPatch.hpp"
#include "cloakframe/OrtAcceleration.hpp"
#include "cloakframe/OutputPlan.hpp"
#include "cloakframe/PathSafety.hpp"
#include "cloakframe/PlateDetector.hpp"
#include "cloakframe/ProcessorWorker.hpp"
#include "cloakframe/ReviewTypes.hpp"
#include "cloakframe/ScrfdFaceDetector.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#ifdef CLOAKFRAME_HAVE_EXIV2
#include <exiv2/exiv2.hpp>
#endif

namespace
{
    class CopyOriginalReviewer final : public QObject
    {
        Q_OBJECT

    public slots:
        cloakframe::ReviewResult requestReview(const QImage &, const QString &,
                                             const QVector<QRectF> &, int, int, double)
        {
            cloakframe::ReviewResult result;
            result.decision = cloakframe::ReviewDecision::CopyOriginal;
            return result;
        }
    };

    class ReplacingCopyOriginalReviewer final : public QObject
    {
        Q_OBJECT

    public:
        ReplacingCopyOriginalReviewer(QString source, QString replacement)
            : source_(std::move(source)), replacement_(std::move(replacement))
        {
        }

    public slots:
        cloakframe::ReviewResult requestReview(const QImage &, const QString &,
                                             const QVector<QRectF> &, int, int, double)
        {
            assert(QFile::remove(source_));
            assert(QFile::rename(replacement_, source_));
            cloakframe::ReviewResult result;
            result.decision = cloakframe::ReviewDecision::CopyOriginal;
            return result;
        }

    private:
        QString source_;
        QString replacement_;
    };

    void testAccelerationBackendNames()
    {
        assert(std::string(cloakframe::ortAcceleratorName(cloakframe::OrtAccelerator::None)) == "CPU");
        assert(std::string(cloakframe::ortAcceleratorName(cloakframe::OrtAccelerator::CoreML)) == "CoreML");
        assert(std::string(cloakframe::ortAcceleratorName(cloakframe::OrtAccelerator::DirectML)) == "DirectML");
        assert(std::string(cloakframe::ortAcceleratorName(cloakframe::OrtAccelerator::CUDA)) == "CUDA");
        assert(std::string(cloakframe::ortAcceleratorName(cloakframe::OrtAccelerator::MIGraphX)) == "MIGraphX");
        assert(std::string(cloakframe::ortAcceleratorName(cloakframe::OrtAccelerator::ROCm)) == "ROCm");
    }

    void testBuiltinModelDigests()
    {
        for (const auto &model: cloakframe::builtinModels())
        {
            const QByteArray digest = QByteArray::fromHex(model.sha256.toLatin1());
            assert(cloakframe::modelDigestMatches(model, digest));
            QByteArray changed = digest;
            changed[0] = static_cast<char>(changed[0] ^ 0x01);
            assert(!cloakframe::modelDigestMatches(model, changed));
        }
        const auto &plate = cloakframe::plateModel();
        assert(cloakframe::modelDigestMatches(
            plate, QByteArray::fromHex(plate.sha256.toLatin1())));
    }

    void writeBytes(const QString &path)
    {
        QFile file(path);
        assert(file.open(QIODevice::WriteOnly));
        assert(file.write("x") == 1);
    }

    void writeBytes(const std::filesystem::path &path, const std::vector<uchar> &bytes)
    {
        QFile file(QString::fromStdString(path.string()));
        assert(file.open(QIODevice::WriteOnly));
        assert(file.write(reinterpret_cast<const char *>(bytes.data()),
                          static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
    }

    void writeJpegWithExifOrientation(const std::filesystem::path &path,
                                      const unsigned char orientation)
    {
        std::vector<uchar> jpeg;
        cv::Mat pixels(2, 3, CV_8UC3, cv::Scalar(20, 40, 60));
        assert(cv::imencode(".jpg", pixels, jpeg));
        assert(jpeg.size() >= 2 && jpeg[0] == 0xFF && jpeg[1] == 0xD8);

        const std::vector<uchar> exif = {
            0xFF, 0xE1, 0x00, 0x22,
            'E', 'x', 'i', 'f', 0x00, 0x00,
            'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,
            0x01, 0x00,
            0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
            orientation, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        };
        jpeg.insert(jpeg.begin() + 2, exif.begin(), exif.end());

        QFile file(QString::fromStdString(path.string()));
        assert(file.open(QIODevice::WriteOnly));
        assert(file.write(reinterpret_cast<const char *>(jpeg.data()),
                          static_cast<qint64>(jpeg.size())) == static_cast<qint64>(jpeg.size()));
    }

    void testSupportedImageExtensions()
    {
        assert(cloakframe::isSupportedImage("photo.jpg"));
        assert(cloakframe::isSupportedImage("photo.JPEG"));
        assert(cloakframe::isSupportedImage("photo.webp"));
        assert(!cloakframe::isSupportedImage("photo.txt"));
    }

    void testScanImagesRecursesAndDeduplicates()
    {
        QTemporaryDir temp;
        assert(temp.isValid());

        QDir root(temp.path());
        assert(root.mkpath("a/nested"));
        assert(root.mkpath("b"));

        const QString first = root.filePath("a/one.JPG");
        const QString second = root.filePath("a/nested/two.png");
        const QString ignored = root.filePath("b/notes.txt");
        writeBytes(first);
        writeBytes(second);
        writeBytes(ignored);

        const auto nonRecursive = cloakframe::scanImages({root.filePath("a")}, false);
        assert(nonRecursive.size() == 1);

        const auto recursive = cloakframe::scanImages({root.filePath("a"), first}, true);
        assert(recursive.size() == 2);

        std::set<std::string> relativePaths;
        for (const auto &item: recursive)
        {
            relativePaths.insert(item.relativePath.generic_string());
        }
        assert(relativePaths.contains("one.JPG"));
        assert(relativePaths.contains("nested/two.png"));
    }

    void testOutputPlanRejectsExistingAndDuplicateDestinations()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const std::filesystem::path root(temp.path().toStdString());
        const auto output = root / "out";
        assert(std::filesystem::create_directories(output / "nested"));

        const std::vector<cloakframe::ScanResult> unique = {
            {root / "input" / "one.jpg", "one.jpg"},
            {root / "input" / "two.mov", "nested/two.mov"},
        };
        assert(cloakframe::findOutputConflicts(unique, output).empty());
        assert(cloakframe::outputRelativePath(unique[1]) == std::filesystem::path("nested/two.mp4"));

        writeBytes(QString::fromStdString((output / "one.jpg").string()));
        const auto existing = cloakframe::findOutputConflicts(unique, output);
        assert(existing.size() == 1);
        assert(existing[0].kind == cloakframe::OutputConflict::Kind::ExistingDestination);

        const std::vector<cloakframe::ScanResult> duplicate = {
            {root / "a" / "same.jpg", "same.jpg"},
            {root / "b" / "same.jpg", "same.jpg"},
        };
        const auto collisions = cloakframe::findOutputConflicts(duplicate, output);
        assert(collisions.size() == 1);
        assert(collisions[0].kind == cloakframe::OutputConflict::Kind::DuplicateDestination);
    }

    void testWorkerReportsUnredactedOutputAsWarningAndPreservesIt()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::path(temp.path().toStdString());
        const auto source = root / "input.png";
        const auto output = root / "out";
        assert(cv::imwrite(source.string(), cv::Mat(24, 24, CV_8UC3, cv::Scalar(20, 40, 60))));

        auto makeWorker = [&]
        {
            cloakframe::ProcessingRequest request;
            request.inputs = {QString::fromStdString(source.string())};
            request.outputDirectory = QString::fromStdString(output.string());
            request.recursive = false;
            request.detectFaces = false;
            return std::make_unique<cloakframe::ProcessorWorker>(std::move(request));
        };

        cloakframe::RunOutcome firstOutcome = cloakframe::RunOutcome::Failed;
        cloakframe::RunSummary firstSummary;
        auto first = makeWorker();
        QObject::connect(first.get(), &cloakframe::ProcessorWorker::summaryAvailable,
                         [&](const cloakframe::RunSummary summary) { firstSummary = summary; });
        QObject::connect(first.get(), &cloakframe::ProcessorWorker::finished,
                         [&](const cloakframe::RunOutcome outcome) { firstOutcome = outcome; });
        first->process();
        assert(firstOutcome == cloakframe::RunOutcome::CompletedWithWarnings);
        assert(firstSummary.total == 1 && firstSummary.redacted == 0 &&
               firstSummary.unredacted == 1);
        assert(std::filesystem::exists(output / "input.png"));

        const auto savedSize = std::filesystem::file_size(output / "input.png");
        cloakframe::RunOutcome secondOutcome = cloakframe::RunOutcome::Completed;
        auto second = makeWorker();
        QObject::connect(second.get(), &cloakframe::ProcessorWorker::finished,
                         [&](const cloakframe::RunOutcome outcome) { secondOutcome = outcome; });
        second->process();
        assert(secondOutcome == cloakframe::RunOutcome::Failed);
        assert(std::filesystem::file_size(output / "input.png") == savedSize);
    }

    void testWorkerReportsCopiedOriginalAsWarning()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::path(temp.path().toStdString());
        const auto source = root / "input.png";
        const auto output = root / "out";
        assert(cv::imwrite(source.string(), cv::Mat(24, 24, CV_8UC3,
                                                    cv::Scalar(20, 40, 60))));

        qRegisterMetaType<cloakframe::ReviewResult>("cloakframe::ReviewResult");
        QThread reviewThread;
        auto *reviewer = new CopyOriginalReviewer;
        reviewer->moveToThread(&reviewThread);
        QObject::connect(&reviewThread, &QThread::finished,
                         reviewer, &QObject::deleteLater);
        reviewThread.start();

        cloakframe::ProcessingRequest request;
        request.inputs = {QString::fromStdString(source.string())};
        request.outputDirectory = QString::fromStdString(output.string());
        request.detectFaces = false;
        request.reviewEnabled = true;
        request.reviewReceiver = reviewer;

        cloakframe::RunOutcome result = cloakframe::RunOutcome::Failed;
        cloakframe::RunSummary summary;
        cloakframe::ProcessorWorker worker(std::move(request));
        QObject::connect(&worker, &cloakframe::ProcessorWorker::summaryAvailable,
                         [&](const cloakframe::RunSummary value) { summary = value; });
        QObject::connect(&worker, &cloakframe::ProcessorWorker::finished,
                         [&](const cloakframe::RunOutcome value) { result = value; });
        worker.process();

        reviewThread.quit();
        assert(reviewThread.wait());
        assert(result == cloakframe::RunOutcome::CompletedWithWarnings);
        assert(summary.total == 1 && summary.copied == 1);
        assert(std::filesystem::exists(output / "input.png"));
    }

    void testWorkerUsesStableImageSnapshotDuringReview()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::path(temp.path().toStdString());
        const auto source = root / "input.png";
        const auto replacement = root / "replacement.png";
        const auto output = root / "out";
        assert(cv::imwrite(source.string(), cv::Mat(32, 32, CV_8UC3,
                                                    cv::Scalar(10, 20, 30))));
        assert(cv::imwrite(replacement.string(), cv::Mat(32, 32, CV_8UC3,
                                                         cv::Scalar(200, 210, 220))));
        QFile originalFile(QString::fromStdString(source.string()));
        assert(originalFile.open(QIODevice::ReadOnly));
        const QByteArray originalBytes = originalFile.readAll();
        originalFile.close();

        QThread reviewThread;
        auto *reviewer = new ReplacingCopyOriginalReviewer(
            QString::fromStdString(source.string()),
            QString::fromStdString(replacement.string()));
        reviewer->moveToThread(&reviewThread);
        QObject::connect(&reviewThread, &QThread::finished,
                         reviewer, &QObject::deleteLater);
        reviewThread.start();

        cloakframe::ProcessingRequest request;
        request.inputs = {QString::fromStdString(source.string())};
        request.outputDirectory = QString::fromStdString(output.string());
        request.detectFaces = false;
        request.reviewEnabled = true;
        request.reviewReceiver = reviewer;
        request.preserveMetadata = true;

        cloakframe::RunOutcome result = cloakframe::RunOutcome::Failed;
        cloakframe::ProcessorWorker worker(std::move(request));
        QObject::connect(&worker, &cloakframe::ProcessorWorker::finished,
                         [&](const cloakframe::RunOutcome value) { result = value; });
        worker.process();

        reviewThread.quit();
        assert(reviewThread.wait());
        assert(result == cloakframe::RunOutcome::CompletedWithWarnings);
        QFile outputFile(QString::fromStdString((output / "input.png").string()));
        assert(outputFile.open(QIODevice::ReadOnly));
        assert(outputFile.readAll() == originalBytes);
    }

    void testWorkerAcceptsThirtyMegabyteJpeg()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::path(temp.path().toStdString());
        const auto source = root / "large.jpg";
        const auto output = root / "out";
        assert(cv::imwrite(source.string(), cv::Mat(64, 64, CV_8UC3,
                                                    cv::Scalar(40, 80, 120))));
        std::filesystem::resize_file(source, 31ULL * 1024ULL * 1024ULL);

        cloakframe::ProcessingRequest request;
        request.inputs = {QString::fromStdString(source.string())};
        request.outputDirectory = QString::fromStdString(output.string());
        request.detectFaces = false;

        cloakframe::RunOutcome result = cloakframe::RunOutcome::Failed;
        cloakframe::ProcessorWorker worker(std::move(request));
        QObject::connect(&worker, &cloakframe::ProcessorWorker::finished,
                         [&](const cloakframe::RunOutcome value) { result = value; });
        worker.process();
        assert(result == cloakframe::RunOutcome::CompletedWithWarnings);
        assert(std::filesystem::exists(output / "large.jpg"));
    }

    void testWorkerRejectsMultiFrameImages()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::path(temp.path().toStdString());
        const auto source = root / "pages.tiff";
        const auto output = root / "out";

        const std::vector<cv::Mat> pages = {
            cv::Mat(12, 16, CV_8UC3, cv::Scalar(20, 40, 60)),
            cv::Mat(12, 16, CV_8UC3, cv::Scalar(80, 100, 120)),
        };
        assert(cv::imwritemulti(source.string(), pages));
        assert(cloakframe::imageFrameCount(source) == 2);

        cloakframe::ProcessingRequest request;
        request.inputs = {QString::fromStdString(source.string())};
        request.outputDirectory = QString::fromStdString(output.string());
        request.detectFaces = false;

        cloakframe::RunOutcome result = cloakframe::RunOutcome::Failed;
        cloakframe::RunSummary summary;
        QStringList messages;
        cloakframe::ProcessorWorker worker(std::move(request));
        QObject::connect(&worker, &cloakframe::ProcessorWorker::summaryAvailable,
                         [&](const cloakframe::RunSummary value) { summary = value; });
        QObject::connect(&worker, &cloakframe::ProcessorWorker::logMessage,
                         [&](const QString &message) { messages.push_back(message); });
        QObject::connect(&worker, &cloakframe::ProcessorWorker::finished,
                         [&](const cloakframe::RunOutcome value) { result = value; });
        worker.process();

        assert(result == cloakframe::RunOutcome::CompletedWithWarnings);
        assert(summary.total == 1 && summary.skipped == 1);
        assert(!std::filesystem::exists(output / "pages.tiff"));
        assert(std::ranges::any_of(messages, [](const QString &message)
        {
            return message.contains("multi-page", Qt::CaseInsensitive);
        }));
    }

    void testAnimatedImageContainersAreDetectedWithoutDecodingAllFrames()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::path(temp.path().toStdString());

        const auto apng = root / "animated.png";
        writeBytes(apng, {
            0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x08, 'a', 'c', 'T', 'L',
            0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        });
        assert(cloakframe::imageFrameCount(apng) > 1);

        const auto webp = root / "animated.webp";
        writeBytes(webp, {
            'R', 'I', 'F', 'F', 0x12, 0x00, 0x00, 0x00,
            'W', 'E', 'B', 'P',
            'V', 'P', '8', 'X', 0x0A, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        });
        assert(cloakframe::imageFrameCount(webp) > 1);

        const auto bigTiff = root / "pages-bigtiff.tiff";
        writeBytes(bigTiff, {
            'I', 'I', 0x2B, 0x00, 0x08, 0x00, 0x00, 0x00,
            0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        });
        assert(cloakframe::imageFrameCount(bigTiff) > 1);
    }

    void testApplyMosaicTouchesOnlyDetectedRegion()
    {
        cv::Mat image(8, 8, CV_8UC3);
        for (int y = 0; y < image.rows; ++y)
        {
            for (int x = 0; x < image.cols; ++x)
            {
                image.at<cv::Vec3b>(y, x) = cv::Vec3b(static_cast<uchar>(x * 20),
                                                      static_cast<uchar>(y * 20),
                                                      static_cast<uchar>((x + y) * 10));
            }
        }

        const cv::Vec3b outsideBefore = image.at<cv::Vec3b>(0, 0);
        const cv::Vec3b insideBefore = image.at<cv::Vec3b>(3, 3);

        cloakframe::FaceDetections detections;
        detections.push_back({cv::Rect2f(2.0F, 2.0F, 4.0F, 4.0F), 1.0F});
        cloakframe::applyMosaic(image, detections, 4, 0.0F);

        assert(image.at<cv::Vec3b>(0, 0) == outsideBefore);
        assert(image.at<cv::Vec3b>(3, 3) != insideBefore);
    }

    void testSoftEdgesKeepDetectedRegionFullyCovered()
    {
        cv::Mat image(64, 64, CV_8UC3, cv::Scalar(100, 100, 100));
        const cv::Rect box(24, 24, 16, 16);

        cloakframe::FaceDetections detections;
        detections.push_back({cv::Rect2f(box), 1.0F});
        cloakframe::applyAnonymization(image, detections, cloakframe::AnonymizationMethod::Fill,
                                     4, 0.0F, cloakframe::MaskShape::Rectangle, true);

        for (int y = box.y; y < box.y + box.height; ++y)
        {
            for (int x = box.x; x < box.x + box.width; ++x)
            {
                assert(image.at<cv::Vec3b>(y, x) == cv::Vec3b(0, 0, 0));
            }
        }

        assert(image.at<cv::Vec3b>(0, 0) == cv::Vec3b(100, 100, 100));

        const cv::Vec3b feathered = image.at<cv::Vec3b>(box.y + box.height / 2, box.x + box.width + 1);
        assert(feathered != cv::Vec3b(0, 0, 0));
        assert(feathered != cv::Vec3b(100, 100, 100));
    }

    void testSoftEdgesEllipseKeepsCoreCovered()
    {
        cv::Mat image(64, 64, CV_8UC3, cv::Scalar(100, 100, 100));
        const cv::Rect box(20, 20, 24, 24);

        cloakframe::FaceDetections detections;
        detections.push_back({cv::Rect2f(box), 1.0F});
        cloakframe::applyAnonymization(image, detections, cloakframe::AnonymizationMethod::Fill,
                                     4, 0.0F, cloakframe::MaskShape::Ellipse, true);

        const int centerX = box.x + box.width / 2;
        const int centerY = box.y + box.height / 2;
        for (int dy = -box.height / 4; dy <= box.height / 4; ++dy)
        {
            for (int dx = -box.width / 4; dx <= box.width / 4; ++dx)
            {
                assert(image.at<cv::Vec3b>(centerY + dy, centerX + dx) == cv::Vec3b(0, 0, 0));
            }
        }

        assert(image.at<cv::Vec3b>(0, 0) == cv::Vec3b(100, 100, 100));
    }

    void testSoftEdgesAtImageBorderStayInBounds()
    {
        cv::Mat image(32, 32, CV_8UC3, cv::Scalar(100, 100, 100));

        cloakframe::FaceDetections detections;
        detections.push_back({cv::Rect2f(0.0F, 0.0F, 12.0F, 12.0F), 1.0F});
        detections.push_back({cv::Rect2f(24.0F, 24.0F, 8.0F, 8.0F), 1.0F});
        cloakframe::applyAnonymization(image, detections, cloakframe::AnonymizationMethod::Fill,
                                     4, 0.0F, cloakframe::MaskShape::Rectangle, true);

        assert(image.at<cv::Vec3b>(0, 0) == cv::Vec3b(0, 0, 0));
        assert(image.at<cv::Vec3b>(31, 31) == cv::Vec3b(0, 0, 0));
    }

    void testSoftEdgesUsePaddingForAGradualTransition()
    {
        cv::Mat image(96, 96, CV_8UC3, cv::Scalar(100, 100, 100));
        const cv::Rect box(32, 32, 32, 32);

        cloakframe::FaceDetections detections;
        detections.push_back({cv::Rect2f(box), 1.0F});
        cloakframe::applyAnonymization(image, detections, cloakframe::AnonymizationMethod::Fill,
                                     4, 0.25F, cloakframe::MaskShape::Rectangle, true);

        for (int y = box.y; y < box.y + box.height; ++y)
        {
            for (int x = box.x; x < box.x + box.width; ++x)
            {
                assert(image.at<cv::Vec3b>(y, x) == cv::Vec3b(0, 0, 0));
            }
        }

        const cv::Vec3b innerBlend = image.at<cv::Vec3b>(48, 25);
        assert(innerBlend != cv::Vec3b(0, 0, 0));
        assert(innerBlend != cv::Vec3b(100, 100, 100));

        const cv::Vec3b outerBlend = image.at<cv::Vec3b>(48, 23);
        assert(outerBlend != cv::Vec3b(0, 0, 0));
        assert(outerBlend != cv::Vec3b(100, 100, 100));
        assert(image.at<cv::Vec3b>(48, 16) == cv::Vec3b(100, 100, 100));
    }

    void testLargeSoftEdgeMaskUsesBoundedWorkingMemoryPath()
    {
        cv::Mat image(1500, 1500, CV_8UC3, cv::Scalar(100, 100, 100));
        cloakframe::FaceDetections detections;
        detections.push_back({cv::Rect2f(100.0F, 100.0F, 1300.0F, 1300.0F), 1.0F});

        cloakframe::applyAnonymization(image, detections, cloakframe::AnonymizationMethod::Fill,
                                     8, 0.0F, cloakframe::MaskShape::Rectangle, true);

        assert(image.at<cv::Vec3b>(750, 750) == cv::Vec3b(0, 0, 0));
        assert(image.at<cv::Vec3b>(0, 0) == cv::Vec3b(100, 100, 100));
        const auto feathered = image.at<cv::Vec3b>(750, 30);
        assert(feathered != cv::Vec3b(0, 0, 0));
        assert(feathered != cv::Vec3b(100, 100, 100));
    }

    void testStickerCoversDetectedRegion()
    {
        cv::Mat image(96, 96, CV_8UC3, cv::Scalar(100, 100, 100));
        const cv::Rect box(32, 32, 32, 32);

        cloakframe::FaceDetections detections;
        detections.push_back({cv::Rect2f(box), 1.0F});
        cloakframe::applyAnonymization(image, detections,
                                     cloakframe::AnonymizationMethod::Sticker,
                                     4, 0.0F, cloakframe::MaskShape::Ellipse, true);

        for (int y = box.y; y < box.y + box.height; ++y)
        {
            for (int x = box.x; x < box.x + box.width; ++x)
            {
                assert(image.at<cv::Vec3b>(y, x) != cv::Vec3b(100, 100, 100));
            }
        }
        assert(image.at<cv::Vec3b>(0, 0) == cv::Vec3b(100, 100, 100));
        assert(image.at<cv::Vec3b>(48, 48) != image.at<cv::Vec3b>(44, 43));
    }

    void testOrientationTransforms()
    {
        cv::Mat base(2, 3, CV_8UC1);
        for (int r = 0; r < base.rows; ++r)
        {
            for (int c = 0; c < base.cols; ++c)
            {
                base.at<uchar>(r, c) = static_cast<uchar>(r * 10 + c);
            }
        }

        cv::Mat identity = base.clone();
        cloakframe::applyOrientation(identity, 1);
        assert(cv::countNonZero(identity != base) == 0);

        cv::Mat rotated = base.clone();
        cloakframe::applyOrientation(rotated, 6);
        assert(rotated.rows == 3 && rotated.cols == 2);
        assert(rotated.at<uchar>(0, 0) == base.at<uchar>(base.rows - 1, 0));

        cv::Mat mirrored = base.clone();
        cloakframe::applyOrientation(mirrored, 2);
        assert(mirrored.rows == 2 && mirrored.cols == 3);
        assert(mirrored.at<uchar>(0, 0) == base.at<uchar>(0, base.cols - 1));
    }

    void testExifOrientationFallback()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::path(temp.path().toStdString());
        for (unsigned char orientation = 1; orientation <= 8; ++orientation)
        {
            const auto path = root / ("orientation-" + std::to_string(orientation) + ".jpg");
            writeJpegWithExifOrientation(path, orientation);
            assert(cloakframe::readExifOrientation(path) == orientation);
        }

        const auto path = root / "orientation-6.jpg";
        cv::Mat image = cloakframe::imreadUnicode(path, cv::IMREAD_UNCHANGED);
        assert(image.rows == 2 && image.cols == 3);
        cloakframe::applyOrientation(image, cloakframe::readExifOrientation(path));
        assert(image.rows == 3 && image.cols == 2);
    }

    void testEncodeParams()
    {
        const auto jpeg = cloakframe::encodeParamsForExtension(".JPG");
        assert(std::find(jpeg.begin(), jpeg.end(), cv::IMWRITE_JPEG_QUALITY) != jpeg.end());
        assert(std::find(jpeg.begin(), jpeg.end(), 100) != jpeg.end());

        const auto png = cloakframe::encodeParamsForExtension("png");
        assert(std::find(png.begin(), png.end(), cv::IMWRITE_PNG_COMPRESSION) != png.end());

        assert(cloakframe::encodeParamsForExtension(".bmp").empty());
    }

    void testImageWritePublishesWithoutReplacing()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::path(temp.path().toStdString());
        const auto destination = root / "result.png";

        writeBytes(QString::fromStdString(destination.string()));
        const cv::Mat first(128, 128, CV_8UC3, cv::Scalar(20, 40, 60));
        assert(!cloakframe::imwriteUnicodeNoReplace(destination, first));
        assert(std::filesystem::file_size(destination) == 1);
        assert(std::filesystem::remove(destination));

        std::barrier start(3);
        std::atomic<int> successes{0};
        const cv::Mat second(512, 512, CV_8UC3, cv::Scalar(80, 100, 120));
        const auto write = [&](const cv::Mat &image)
        {
            start.arrive_and_wait();
            if (cloakframe::imwriteUnicodeNoReplace(destination, image))
            {
                successes.fetch_add(1, std::memory_order_relaxed);
            }
        };
        std::thread firstWriter(write, std::cref(first));
        std::thread secondWriter(write, std::cref(second));
        start.arrive_and_wait();
        firstWriter.join();
        secondWriter.join();

        assert(successes.load(std::memory_order_relaxed) == 1);
        assert(!cv::imread(destination.string(), cv::IMREAD_UNCHANGED).empty());
        for (const auto &entry: std::filesystem::directory_iterator(root))
        {
            assert(entry.path().filename().string().find(".cloakframe-") == std::string::npos);
        }
    }

    void testRootedWritesRejectEscapesAndUsePrivateFiles()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto base = std::filesystem::canonical(
            std::filesystem::path(temp.path().toStdString()));
        const auto root = base / "output";
        const auto outside = base / "outside";
        assert(std::filesystem::create_directories(root));
        assert(std::filesystem::create_directories(outside));

        const cv::Mat image(32, 32, CV_8UC3, cv::Scalar(30, 60, 90));
        assert(cloakframe::imwriteUnicodeNoReplaceAtRoot(
                   root, "nested/result.png", image) == cloakframe::ImageWriteResult::Saved);
        assert(!cv::imread((root / "nested/result.png").string()).empty());
        assert(cloakframe::imwriteUnicodeNoReplaceAtRoot(
                   root, "../outside/escaped.png", image) == cloakframe::ImageWriteResult::Failed);
        assert(!std::filesystem::exists(outside / "escaped.png"));

        const auto source = base / "original.bin";
        const std::vector<uchar> sourceBytes = {0, 1, 2, 3, 4, 5, 0xFE, 0xFF};
        writeBytes(source, sourceBytes);
#ifndef _WIN32
        assert(::chmod(source.c_str(), 0600) == 0);
#endif
        assert(cloakframe::copyFileNoReplaceAtRoot(source, root, "copies/original.bin"));
        std::ifstream copied(root / "copies/original.bin", std::ios::binary);
        const std::istreambuf_iterator<char> copiedBegin(copied);
        const std::istreambuf_iterator<char> copiedEnd;
        const std::vector<uchar> copiedBytes(copiedBegin, copiedEnd);
        assert(copiedBytes == sourceBytes);

        const auto moveSource = base / "move-source.bin";
        writeBytes(moveSource, sourceBytes);
        assert(cloakframe::moveFileNoReplaceAtRoot(
                   moveSource, root, "moves/moved.bin") ==
               cloakframe::FileMoveResult::Moved);
        assert(!std::filesystem::exists(moveSource));
        std::ifstream moved(root / "moves/moved.bin", std::ios::binary);
        const std::istreambuf_iterator<char> movedBegin(moved);
        const std::istreambuf_iterator<char> movedEnd;
        const std::vector<uchar> movedBytes(movedBegin, movedEnd);
        assert(movedBytes == sourceBytes);

        const auto blockedSource = base / "blocked-source.bin";
        const auto blockedDestination = root / "moves/existing.bin";
        const std::vector<uchar> existingBytes = {9, 8, 7, 6};
        writeBytes(blockedSource, sourceBytes);
        writeBytes(blockedDestination, existingBytes);
#ifndef _WIN32
        assert(::chmod(blockedSource.c_str(), 0640) == 0);
#endif
        assert(cloakframe::moveFileNoReplaceAtRoot(
                   blockedSource, root, "moves/existing.bin") ==
               cloakframe::FileMoveResult::Failed);
        assert(std::filesystem::exists(blockedSource));
        std::ifstream existing(blockedDestination, std::ios::binary);
        const std::istreambuf_iterator<char> existingBegin(existing);
        const std::istreambuf_iterator<char> existingEnd;
        const std::vector<uchar> preservedBytes(existingBegin, existingEnd);
        assert(preservedBytes == existingBytes);
#ifndef _WIN32
        struct stat blockedStatus{};
        assert(::stat(blockedSource.c_str(), &blockedStatus) == 0);
        assert((blockedStatus.st_mode & 0777) == 0640);
#endif

        const auto guardedSource = base / "guarded-source.bin";
        writeBytes(guardedSource, sourceBytes);
        assert(cloakframe::moveFileNoReplaceAtRoot(
                   guardedSource, root, "moves/guarded.bin", []
                   {
                       return false;
                   }) == cloakframe::FileMoveResult::Failed);
        assert(std::filesystem::exists(guardedSource));
        assert(!std::filesystem::exists(root / "moves/guarded.bin"));
#ifndef _WIN32
        struct stat copiedStatus{};
        assert(::stat((root / "copies/original.bin").c_str(), &copiedStatus) == 0);
        assert((copiedStatus.st_mode & 0777) == 0600);

        std::error_code ec;
        std::filesystem::create_directory_symlink(outside, root / "linked", ec);
        assert(!ec);
        assert(cloakframe::imwriteUnicodeNoReplaceAtRoot(
                   root, "linked/escaped.png", image) == cloakframe::ImageWriteResult::Failed);
        assert(!std::filesystem::exists(outside / "escaped.png"));
#endif

        for (const auto &entry: std::filesystem::recursive_directory_iterator(root))
        {
            assert(!entry.path().filename().string().starts_with(".cloakframe-"));
        }
    }

    void testMetadataFailurePublishesCleanImage()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::canonical(
            std::filesystem::path(temp.path().toStdString()));
        const cv::Mat image(24, 24, CV_8UC3, cv::Scalar(15, 45, 75));
        const auto result = cloakframe::imwriteUnicodeNoReplaceAtRoot(
            root, "result.jpg", image, cloakframe::encodeParamsForExtension("jpg"),
            root / "missing-metadata-source.jpg");
        assert(result == cloakframe::ImageWriteResult::SavedWithoutMetadata);
        assert(!cv::imread((root / "result.jpg").string()).empty());
    }

    void testIntersectionOverUnion()
    {
        const cv::Rect2f a(0.0F, 0.0F, 10.0F, 10.0F);
        assert(std::abs(cloakframe::intersectionOverUnion(a, a) - 1.0F) < 1e-5F);

        const cv::Rect2f disjoint(100.0F, 100.0F, 10.0F, 10.0F);
        assert(cloakframe::intersectionOverUnion(a, disjoint) == 0.0F);

        const cv::Rect2f empty(0.0F, 0.0F, 0.0F, 0.0F);
        assert(cloakframe::intersectionOverUnion(a, empty) == 0.0F);

        const cv::Rect2f halfShifted(5.0F, 0.0F, 10.0F, 10.0F);
        assert(std::abs(cloakframe::intersectionOverUnion(a, halfShifted) - (50.0F / 150.0F)) < 1e-5F);
    }

    void testNonMaxSuppression()
    {
        cloakframe::FaceDetections detections;
        detections.push_back({cv::Rect2f(0.0F, 0.0F, 10.0F, 10.0F), 0.9F});
        detections.push_back({cv::Rect2f(1.0F, 1.0F, 10.0F, 10.0F), 0.8F});
        detections.push_back({cv::Rect2f(100.0F, 100.0F, 10.0F, 10.0F), 0.7F});

        const auto kept = cloakframe::nonMaxSuppression(detections, 0.4F);
        assert(kept.size() == 2);
        assert(kept[0].score == 0.9F);
        assert(kept[1].box.x == 100.0F);
    }

    void testInvalidDetectionsAreIgnored()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        cloakframe::FaceDetections detections = {
            {cv::Rect2f(nan, 0.0F, 10.0F, 10.0F), 0.9F},
            {cv::Rect2f(0.0F, 0.0F, 10.0F, 10.0F), nan},
            {cv::Rect2f(2.0F, 2.0F, 8.0F, 8.0F), 0.8F},
        };
        const auto kept = cloakframe::nonMaxSuppression(detections, 0.4F);
        assert(kept.size() == 1);

        cv::Mat image(16, 16, CV_8UC3, cv::Scalar(30, 60, 90));
        const cv::Mat before = image.clone();
        cloakframe::applyAnonymization(
            image, {{cv::Rect2f(0.0F, nan, 10.0F, 10.0F), 0.9F}},
            cloakframe::AnonymizationMethod::Fill, 8, 0.0F);
        assert(cv::norm(image, before, cv::NORM_INF) == 0.0);
    }

    void testDestinationPathSafety()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const std::filesystem::path root =
            std::filesystem::path(temp.path().toStdString()) / "out";
        assert(std::filesystem::create_directories(root));

        assert(cloakframe::destinationIsSafe(root / "a.jpg", root));
        assert(cloakframe::destinationIsSafe(root / "sub" / "b.png", root));
        assert(cloakframe::destinationIsSafe(root, root));

        assert(!cloakframe::destinationIsSafe(root / ".." / "escape.jpg", root));

        assert(cloakframe::isWithinRoot(root / "x.jpg", root));
        assert(!cloakframe::isWithinRoot(root.parent_path() / "x.jpg", root));
    }

#ifndef _WIN32
    void testDestinationRejectsSymlinkEscape()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const std::filesystem::path base = std::filesystem::path(temp.path().toStdString());
        const std::filesystem::path root = base / "out";
        const std::filesystem::path outside = base / "outside";
        assert(std::filesystem::create_directories(root));
        assert(std::filesystem::create_directories(outside));

        std::error_code ec;
        const std::filesystem::path link = root / "evil";
        std::filesystem::create_directory_symlink(outside, link, ec);
        assert(!ec);

        assert(!cloakframe::destinationIsSafe(link / "leak.jpg", root));

        const std::filesystem::path danglingTarget = outside / "not-created.jpg";
        const std::filesystem::path danglingLink = root / "dangling.jpg";
        std::filesystem::create_symlink(danglingTarget, danglingLink, ec);
        assert(!ec);
        assert(!std::filesystem::exists(danglingLink));
        assert(std::filesystem::is_symlink(std::filesystem::symlink_status(danglingLink)));
        assert(!cloakframe::destinationIsSafe(danglingLink, root));

        const std::vector<cloakframe::ScanResult> planned = {
            {base / "input" / "dangling.jpg", "dangling.jpg"},
        };
        const auto conflicts = cloakframe::findOutputConflicts(planned, root);
        assert(conflicts.size() == 1);
        assert(conflicts.front().kind ==
               cloakframe::OutputConflict::Kind::ExistingDestination);

        const auto source = base / "source.jpg";
        assert(cv::imwrite(source.string(), cv::Mat(8, 8, CV_8UC3, cv::Scalar(1, 2, 3))));
        assert(!cloakframe::copyFileNoReplace(source, danglingLink));
        assert(!cloakframe::imwriteUnicodeNoReplace(
            danglingLink, cv::Mat(8, 8, CV_8UC3, cv::Scalar(4, 5, 6))));
        assert(!std::filesystem::exists(danglingTarget));
    }
#endif

#ifdef CLOAKFRAME_HAVE_EXIV2
    void testMetadataCopyAndOrientationNormalize()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const auto root = std::filesystem::canonical(
            std::filesystem::path(temp.path().toStdString()));
        const std::filesystem::path src = root / "src.jpg";
        const std::filesystem::path dst = root / "dst.jpg";

        cv::Mat img(16, 16, CV_8UC3, cv::Scalar(120, 120, 120));
        assert(cv::imwrite(src.string(), img));
        assert(cv::imwrite(dst.string(), img));

        {
            auto image = Exiv2::ImageFactory::open(src.string());
            image->readMetadata();
            image->exifData()["Exif.Image.Artist"] = "TestPhotographer";
            image->exifData()["Exif.Image.Orientation"] = static_cast<uint16_t>(6);
            image->exifData()["Exif.Photo.UserComment"] =
                "data:image/jpeg;base64,unsafe-payload";
            image->xmpData()["Xmp.tiff.Orientation"] = "6";
            image->xmpData()["Xmp.dc.description"] =
                "data:image/jpeg;base64,unsafe-payload";
            image->setComment("data:image/jpeg;base64,unsafe-payload");
            image->writeMetadata();
        }

        assert(cloakframe::readExifOrientation(src) == 6);
        assert(cloakframe::copyMetadata(src, dst, true));

        {
            auto image = Exiv2::ImageFactory::open(dst.string());
            image->readMetadata();
            const Exiv2::ExifData &exif = image->exifData();

            const auto artist = exif.findKey(Exiv2::ExifKey("Exif.Image.Artist"));
            assert(artist != exif.end());
            assert(artist->toString() == "TestPhotographer");

            const auto orientation = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
            assert(orientation != exif.end());
#if EXIV2_TEST_VERSION(0, 28, 0)
            assert(orientation->toInt64() == 1);
#else
            assert(orientation->toLong() == 1);
#endif
            assert(image->xmpData().findKey(Exiv2::XmpKey("Xmp.tiff.Orientation")) ==
                   image->xmpData().end());
            assert(image->xmpData().empty());
            assert(image->exifData().findKey(
                       Exiv2::ExifKey("Exif.Photo.UserComment")) ==
                   image->exifData().end());
            assert(image->comment().empty());
        }

        const auto published = root / "published.jpg";
        assert(cloakframe::imwriteUnicodeNoReplaceAtRoot(
                   root, published.filename(), img,
                   cloakframe::encodeParamsForExtension("jpg"), src) ==
               cloakframe::ImageWriteResult::Saved);
        auto publishedMetadata = Exiv2::ImageFactory::open(published.string());
        publishedMetadata->readMetadata();
        const auto artist = publishedMetadata->exifData().findKey(
            Exiv2::ExifKey("Exif.Image.Artist"));
        assert(artist != publishedMetadata->exifData().end());
        assert(artist->toString() == "TestPhotographer");
        assert(publishedMetadata->xmpData().findKey(
                   Exiv2::XmpKey("Xmp.tiff.Orientation")) ==
               publishedMetadata->xmpData().end());
    }

    long exifThumbnailBytes(Exiv2::ExifData &exif)
    {
        Exiv2::ExifThumb thumb(exif);
#if EXIV2_TEST_VERSION(0, 28, 0)
        return static_cast<long>(thumb.copy().size());
#else
        return thumb.copy().size_;
#endif
    }

    void testMetadataCopyStripsEmbeddedThumbnail()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const std::filesystem::path base = std::filesystem::path(temp.path().toStdString());
        const std::filesystem::path src = base / "src.jpg";
        const std::filesystem::path dst = base / "dst.jpg";
        const std::filesystem::path thumbFile = base / "thumb.jpg";

        cv::Mat img(16, 16, CV_8UC3, cv::Scalar(120, 120, 120));
        assert(cv::imwrite(src.string(), img));
        assert(cv::imwrite(dst.string(), img));
        assert(cv::imwrite(thumbFile.string(), img));

        {
            auto image = Exiv2::ImageFactory::open(src.string());
            image->readMetadata();
            image->exifData()["Exif.Image.Artist"] = "TestPhotographer";
            Exiv2::ExifThumb thumb(image->exifData());
            thumb.setJpegThumbnail(thumbFile.string());
            image->writeMetadata();
        }

        {
            auto image = Exiv2::ImageFactory::open(src.string());
            image->readMetadata();
            assert(exifThumbnailBytes(image->exifData()) > 0);
        }

        assert(cloakframe::copyMetadata(src, dst, true));

        {
            auto image = Exiv2::ImageFactory::open(dst.string());
            image->readMetadata();
            assert(exifThumbnailBytes(image->exifData()) == 0);

            const Exiv2::ExifData &exif = image->exifData();
            const auto artist = exif.findKey(Exiv2::ExifKey("Exif.Image.Artist"));
            assert(artist != exif.end());
            assert(artist->toString() == "TestPhotographer");
        }
    }
#endif
}

namespace
{
    void testDetectorsRejectUnexpectedModelHash()
    {
        QTemporaryDir temp;
        assert(temp.isValid());
        const QString modelPath = temp.filePath("model.onnx");
        QFile model(modelPath);
        assert(model.open(QIODevice::WriteOnly));
        assert(model.write("invalid-model") == 13);
        model.close();

        const QByteArray unexpectedHash(32, '\0');
        bool faceRejected = false;
        try
        {
            cloakframe::ScrfdFaceDetector detector(modelPath.toStdString(), 640, false,
                                                  unexpectedHash);
        }
        catch (const std::runtime_error &error)
        {
            faceRejected = std::string(error.what()).find("changed") != std::string::npos;
        }
        assert(faceRejected);

        bool plateRejected = false;
        try
        {
            cloakframe::PlateDetector detector(modelPath.toStdString(), false, unexpectedHash);
        }
        catch (const std::runtime_error &error)
        {
            plateRejected = std::string(error.what()).find("changed") != std::string::npos;
        }
        assert(plateRejected);
    }

    void testOnnxPatchRejectsInvalidBytes()
    {
        assert(!cloakframe::makeOnnxSpatialDimsFixed({}, 320).has_value());
        const std::vector<std::uint8_t> garbage = {0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x9C};
        assert(!cloakframe::makeOnnxSpatialDimsFixed(garbage, 320).has_value());
        assert(!cloakframe::makeOnnxSpatialDimsFixed(garbage, 0).has_value());
        assert(!cloakframe::makeOnnxSpatialDimsFixed(garbage, -320).has_value());
    }

    void testFixedScrfdModelRunsAtRequestedSize()
    {
        const auto modelPath = std::filesystem::path(__FILE__).parent_path().parent_path()
                               / "models" / "2.5g_bnkps.onnx";
        if (!std::filesystem::exists(modelPath))
        {
            std::puts("skipping fixed-model patch test: models/2.5g_bnkps.onnx not present");
            return;
        }

        cloakframe::ScrfdFaceDetector nativeSize(modelPath.string(), 640);
        assert(nativeSize.inputSize() == 640);

        cloakframe::ScrfdFaceDetector patchedSize(modelPath.string(), 320);
        assert(patchedSize.inputSize() == 320);

        const cv::Mat blank(180, 320, CV_8UC3, cv::Scalar(30, 30, 30));
        assert(patchedSize.detect(blank, 0.5F, 0.4F).empty());
    }

    void testDynamicScrfdModelRunsAtRequestedSize()
    {
        const auto modelPath = std::filesystem::path(__FILE__).parent_path().parent_path()
                               / "models" / "10g_bnkps.onnx";
        if (!std::filesystem::exists(modelPath))
        {
            std::puts("skipping dynamic-model patch test: models/10g_bnkps.onnx not present");
            return;
        }

        cloakframe::ScrfdFaceDetector patchedSize(modelPath.string(), 320);
        assert(patchedSize.inputSize() == 320);

        const cv::Mat blank(180, 320, CV_8UC3, cv::Scalar(30, 30, 30));
        assert(patchedSize.detect(blank, 0.5F, 0.4F).empty());
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testAccelerationBackendNames();
    testBuiltinModelDigests();
    testSupportedImageExtensions();
    testScanImagesRecursesAndDeduplicates();
    testOutputPlanRejectsExistingAndDuplicateDestinations();
    testWorkerReportsUnredactedOutputAsWarningAndPreservesIt();
    testWorkerReportsCopiedOriginalAsWarning();
    testWorkerUsesStableImageSnapshotDuringReview();
    testWorkerAcceptsThirtyMegabyteJpeg();
    testWorkerRejectsMultiFrameImages();
    testAnimatedImageContainersAreDetectedWithoutDecodingAllFrames();
    testApplyMosaicTouchesOnlyDetectedRegion();
    testSoftEdgesKeepDetectedRegionFullyCovered();
    testSoftEdgesEllipseKeepsCoreCovered();
    testSoftEdgesAtImageBorderStayInBounds();
    testSoftEdgesUsePaddingForAGradualTransition();
    testLargeSoftEdgeMaskUsesBoundedWorkingMemoryPath();
    testStickerCoversDetectedRegion();
    testOrientationTransforms();
    testExifOrientationFallback();
    testEncodeParams();
    testImageWritePublishesWithoutReplacing();
    testRootedWritesRejectEscapesAndUsePrivateFiles();
    testMetadataFailurePublishesCleanImage();
    testIntersectionOverUnion();
    testNonMaxSuppression();
    testInvalidDetectionsAreIgnored();
    testDetectorsRejectUnexpectedModelHash();
    testOnnxPatchRejectsInvalidBytes();
    testFixedScrfdModelRunsAtRequestedSize();
    testDynamicScrfdModelRunsAtRequestedSize();
    testDestinationPathSafety();
#ifndef _WIN32
    testDestinationRejectsSymlinkEscape();
#endif
#ifdef CLOAKFRAME_HAVE_EXIV2
    testMetadataCopyAndOrientationNormalize();
    testMetadataCopyStripsEmbeddedThumbnail();
#endif
    return 0;
}

#include "test_core.moc"
