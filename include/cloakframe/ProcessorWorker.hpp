#pragma once

#include "cloakframe/ImageScanner.hpp"
#include "cloakframe/Mosaic.hpp"
#include "cloakframe/VideoIo.hpp"

#include <QObject>
#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>

namespace cloakframe
{
    class ScrfdFaceDetector;
    class PlateDetector;

    struct ProcessingRequest
    {
        QString modelPath;
        QByteArray modelSha256;
        QStringList inputs;
        QString outputDirectory;
        QString plateModelPath;
        QByteArray plateModelSha256;
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
        VideoCodec videoCodec = VideoCodec::H264;
    };

    struct DetectorCache
    {
        std::shared_ptr<ScrfdFaceDetector> face;
        std::shared_ptr<PlateDetector> plate;
        std::shared_ptr<ScrfdFaceDetector> videoFace;
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

        [[nodiscard]] std::shared_ptr<ScrfdFaceDetector> takeDetector();

        [[nodiscard]] std::shared_ptr<PlateDetector> takePlateDetector();

        [[nodiscard]] std::shared_ptr<ScrfdFaceDetector> takeVideoDetector();

    public slots:
        void process();

        void cancel();

    signals:
        void progressChanged(int completed, int total);

        void stageChanged(int index, int total, const QString &stage, const QString &fileName);

        void logMessage(const QString &message);

        void summaryAvailable(cloakframe::RunSummary summary);

        void finished(cloakframe::RunOutcome outcome);

    private:
        struct ItemOutcome;

        ItemOutcome processItem(const ScanResult &item,
                                const std::filesystem::path &safeRoot,
                                int index,
                                int total,
                                bool allowReview);

        ItemOutcome processVideoItem(const ScanResult &item,
                                     const std::filesystem::path &safeRoot,
                                     const std::filesystem::path &destination,
                                     int index,
                                     int total);

        QString modelPath_;
        QByteArray modelSha256_;
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
        QByteArray plateModelSha256_;
        bool gpuAcceleration_;
        int videoCrf_;
        VideoCodec videoCodec_;
        std::atomic<bool> cancelled_{false};
        std::mutex imageMemoryMutex_;
        std::condition_variable imageMemoryCv_;
        std::uint64_t imageMemoryAvailable_ = 0;
        std::mutex detectMutex_;
        std::shared_ptr<ScrfdFaceDetector> detector_;
        std::shared_ptr<PlateDetector> plateDetector_;
        std::shared_ptr<ScrfdFaceDetector> videoDetector_;
    };
}

Q_DECLARE_METATYPE(cloakframe::RunOutcome)
Q_DECLARE_METATYPE(cloakframe::RunSummary)
