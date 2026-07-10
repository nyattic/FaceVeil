#pragma once

#include "redactly/ImageScanner.hpp"
#include "redactly/Mosaic.hpp"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>

namespace redactly
{
    class YunetFaceDetector;
    class PlateDetector;

    struct ProcessingRequest
    {
        QString modelPath;
        QStringList inputs;
        QString outputDirectory;
        QString plateModelPath;
        QObject *reviewReceiver = nullptr;
        bool recursive = true;
        float scoreThreshold = 0.5F;
        float nmsThreshold = 0.4F;
        int mosaicBlockSize = 14;
        float paddingRatio = 0.18F;
        AnonymizationMethod method = AnonymizationMethod::Mosaic;
        MaskShape shape = MaskShape::Rectangle;
        bool softEdges = false;
        bool preserveMetadata = false;
        bool reviewEnabled = false;
        bool detectFaces = true;
        bool detectPlates = false;
        bool gpuAcceleration = false;
        int videoCrf = 18;
    };

    struct DetectorCache
    {
        std::shared_ptr<YunetFaceDetector> face;
        std::shared_ptr<PlateDetector> plate;
        std::shared_ptr<YunetFaceDetector> videoFace;
    };

    enum class RunOutcome
    {
        Completed,
        CompletedWithWarnings,
        Cancelled,
        Failed,
    };

    struct RunSummary
    {
        int total = 0;
        int redacted = 0;
        int copied = 0;
        int skipped = 0;
        int failed = 0;
        int unredacted = 0;
    };

    class ProcessorWorker final : public QObject
    {
        Q_OBJECT

    public:
        explicit ProcessorWorker(ProcessingRequest request, DetectorCache cache = {});

        ~ProcessorWorker() override;

        [[nodiscard]] std::shared_ptr<YunetFaceDetector> takeDetector();

        [[nodiscard]] std::shared_ptr<PlateDetector> takePlateDetector();

        [[nodiscard]] std::shared_ptr<YunetFaceDetector> takeVideoDetector();

    public slots:
        void process();

        void cancel();

    signals:
        void progressChanged(int completed, int total);

        void stageChanged(int index, int total, const QString &stage, const QString &fileName);

        void logMessage(const QString &message);

        void summaryAvailable(redactly::RunSummary summary);

        void finished(redactly::RunOutcome outcome);

    private:
        struct ItemOutcome;

        ItemOutcome processItem(const ScanResult &item,
                                const std::filesystem::path &safeRoot,
                                int index,
                                int total,
                                bool allowReview);

        ItemOutcome processVideoItem(const ScanResult &item,
                                     const std::filesystem::path &destination,
                                     int index,
                                     int total);

        QString modelPath_;
        QStringList inputs_;
        QString outputDirectory_;
        bool recursive_;
        float scoreThreshold_;
        float nmsThreshold_;
        int mosaicBlockSize_;
        float paddingRatio_;
        AnonymizationMethod method_;
        MaskShape shape_;
        bool softEdges_;
        bool preserveMetadata_;
        bool reviewEnabled_;
        QPointer<QObject> reviewReceiver_;
        bool detectFaces_;
        bool detectPlates_;
        QString plateModelPath_;
        bool gpuAcceleration_;
        int videoCrf_;
        std::atomic<bool> cancelled_{false};
        std::mutex detectMutex_;
        std::shared_ptr<YunetFaceDetector> detector_;
        std::shared_ptr<PlateDetector> plateDetector_;
        std::shared_ptr<YunetFaceDetector> videoDetector_;
    };
}

Q_DECLARE_METATYPE(redactly::RunOutcome)
Q_DECLARE_METATYPE(redactly::RunSummary)
