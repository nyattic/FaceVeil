#pragma once

#include "cloakframe/VideoReviewTypes.hpp"

#include "cloakframe/ProcessorWorker.hpp"
#include "cloakframe/ReviewTypes.hpp"
#include "cloakframe/Theme.hpp"

#include <QElapsedTimer>
#include <QByteArray>
#include <QMainWindow>
#include <QTranslator>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QShortcut;
class QSpinBox;
class QThread;
class QToolButton;
class QWidget;

namespace cloakframe
{
    struct BuiltinModel;
    class ProcessorWorker;
    class ScrfdFaceDetector;
    class PlateDetector;

    class MainWindow final : public QMainWindow
    {
        Q_OBJECT

    public:
        explicit MainWindow(QWidget *parent = nullptr);

        ~MainWindow() override;

        Q_INVOKABLE cloakframe::ReviewResult requestReview(const QImage &image,
                                                         const QString &sourceName,
                                                         const QVector<QRectF> &detected,
                                                         int currentIndex,
                                                         int total,
                                                         double previewScale);

        Q_INVOKABLE cloakframe::VideoReviewResult requestVideoReview(
            const cloakframe::VideoReviewRequest &request);

    protected:
        void dragEnterEvent(QDragEnterEvent *event) override;

        void dragLeaveEvent(QDragLeaveEvent *event) override;

        void dropEvent(QDropEvent *event) override;

        void closeEvent(QCloseEvent *event) override;

        void changeEvent(QEvent *event) override;

    private slots:
        void chooseModel();

        void downloadSelectedModel();

        void chooseFiles();

        void chooseFolder();

        void chooseOutputDirectory();

        void startProcessing();

        void stopProcessing() const;

        void onWorkerFinished(cloakframe::RunOutcome outcome);

        void toggleAdvanced(bool expanded) const;

        void resetAdvancedDefaults() const;

        void openSettings();

    private:
        void addInputPath(const QString &path) const;

        void populateBundledModels();

        void updateModelPathFromSelection() const;

        [[nodiscard]] QString selectedModelPath() const;

        [[nodiscard]] const BuiltinModel *selectedBuiltinModel() const;

        void setProcessing(bool processing);

        void setDropHighlight(bool active) const;

        void updateSettingsIcon() const;

        void updateAnonymizationSample() const;

        void updateAnonymizationControls() const;

        [[nodiscard]] QStringList inputPaths() const;

        void appendLog(const QString &message) const;

        void reportValidationIssue(const QString &message, QWidget *field = nullptr) const;

        void loadSettings();

        void saveSettings() const;

        void checkForUpdates();

        void retranslateUi();

        void applyLanguage(const QString &language);

        void addRetranslation(std::function<void()> apply);

        struct DetectorCacheKey
        {
            QString canonicalModelPath;
            qint64 modelSize = -1;
            qint64 modelLastModifiedMs = -1;
            QByteArray modelSha256;
            bool gpuAcceleration = false;

            [[nodiscard]] bool isValid() const
            {
                return !canonicalModelPath.isEmpty() && !modelSha256.isEmpty();
            }

            [[nodiscard]] bool operator==(const DetectorCacheKey &other) const
            {
                return canonicalModelPath == other.canonicalModelPath &&
                       modelSize == other.modelSize &&
                       modelLastModifiedMs == other.modelLastModifiedMs &&
                       modelSha256 == other.modelSha256 &&
                       gpuAcceleration == other.gpuAcceleration;
            }
        };

        struct ActiveRunState
        {
            DetectorCacheKey faceKey;
            DetectorCacheKey plateKey;
            AnonymizationMethod method = AnonymizationMethod::Mosaic;
            MaskShape shape = MaskShape::Rectangle;
            int blockSize = 14;
            float padding = 0.18F;
            bool softEdges = false;
            bool preserveMetadata = false;
            bool detectFaces = false;
            bool detectPlates = false;
        };

        [[nodiscard]] static DetectorCacheKey makeDetectorCacheKey(
            const QString &modelPath, bool gpuAcceleration);

        QComboBox *modelCombo_ = nullptr;
        QComboBox *detectCombo_ = nullptr;
        QComboBox *methodCombo_ = nullptr;
        QComboBox *shapeCombo_ = nullptr;
        QCheckBox *softEdgeCheck_ = nullptr;
        QToolButton *settingsButton_ = nullptr;
        QShortcut *settingsShortcut_ = nullptr;
        QLineEdit *modelPathEdit_ = nullptr;
        QPushButton *downloadButton_ = nullptr;
        QPushButton *modelBrowseButton_ = nullptr;
        QLineEdit *outputDirEdit_ = nullptr;
        QPushButton *outputBrowseButton_ = nullptr;
        QListWidget *inputList_ = nullptr;
        QPushButton *addFilesButton_ = nullptr;
        QPushButton *addFolderButton_ = nullptr;
        QPushButton *clearInputsButton_ = nullptr;
        QCheckBox *recursiveCheck_ = nullptr;
        QCheckBox *reviewCheck_ = nullptr;
        QCheckBox *preserveMetaCheck_ = nullptr;
        QLabel *updateLabel_ = nullptr;
        QDoubleSpinBox *scoreThresholdSpin_ = nullptr;
        QDoubleSpinBox *nmsThresholdSpin_ = nullptr;
        QSpinBox *blockSizeSpin_ = nullptr;
        QDoubleSpinBox *paddingSpin_ = nullptr;
        QProgressBar *progressBar_ = nullptr;
        QPlainTextEdit *logEdit_ = nullptr;
        QPushButton *startButton_ = nullptr;
        QPushButton *stopButton_ = nullptr;
        QPushButton *openOutputButton_ = nullptr;
        QLabel *statusLabel_ = nullptr;
        QLabel *samplePreview_ = nullptr;
        QToolButton *advancedToggle_ = nullptr;
        QPushButton *resetAdvancedButton_ = nullptr;
        QWidget *advancedBody_ = nullptr;

        QThread *workerThread_ = nullptr;
        ProcessorWorker *worker_ = nullptr;
        QElapsedTimer runTimer_;

        std::shared_ptr<ScrfdFaceDetector> cachedDetector_;
        std::shared_ptr<ScrfdFaceDetector> cachedVideoDetector_;
        DetectorCacheKey cachedDetectorKey_;
        DetectorCacheKey cachedVideoDetectorKey_;
        std::shared_ptr<PlateDetector> cachedPlateDetector_;
        DetectorCacheKey cachedPlateDetectorKey_;
        std::optional<ActiveRunState> activeRunState_;

        QTranslator translator_;
        QTranslator qtTranslator_;
        QString language_;
        ThemeMode themeMode_ = ThemeMode::System;
        bool checkForUpdatesOnStartup_ = true;
        bool fileLogging_ = true;
        bool gpuAcceleration_ = true;
        int videoQuality_ = 0;
        int videoCodec_ = 0;
        bool processing_ = false;
        bool shuttingDown_ = false;
        RunSummary lastRunSummary_;
        std::vector<std::function<void()>> retranslators_;
    };
}
