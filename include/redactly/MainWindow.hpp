#pragma once

#include "redactly/ProcessorWorker.hpp"
#include "redactly/ReviewTypes.hpp"
#include "redactly/Theme.hpp"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QTranslator>
#include <QVector>

#include <functional>
#include <memory>
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
class QSpinBox;
class QThread;
class QToolButton;
class QWidget;

namespace redactly
{
    class ProcessorWorker;
    class YunetFaceDetector;
    class PlateDetector;

    class MainWindow final : public QMainWindow
    {
        Q_OBJECT

    public:
        explicit MainWindow(QWidget *parent = nullptr);

        ~MainWindow() override;

        Q_INVOKABLE redactly::ReviewResult requestReview(const QImage &image,
                                                         const QString &sourceName,
                                                         const QVector<QRectF> &detected,
                                                         int currentIndex,
                                                         int total,
                                                         double previewScale);

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

        void onWorkerFinished(redactly::RunOutcome outcome);

        void toggleAdvanced(bool expanded) const;

        void resetAdvancedDefaults() const;

        void openSettings();

    private:
        void addInputPath(const QString &path) const;

        void populateBundledModels();

        void updateModelPathFromSelection() const;

        [[nodiscard]] QString selectedModelPath() const;

        void setProcessing(bool processing) const;

        void setDropHighlight(bool active) const;

        void updateSettingsIcon() const;

        void updateAnonymizationSample() const;

        [[nodiscard]] QStringList inputPaths() const;

        void appendLog(const QString &message) const;

        void reportValidationIssue(const QString &message, QWidget *field = nullptr) const;

        void loadSettings();

        void saveSettings() const;

        void checkForUpdates();

        void retranslateUi();

        void applyLanguage(const QString &language);

        void addRetranslation(std::function<void()> apply);

        QComboBox *modelCombo_ = nullptr;
        QComboBox *detectCombo_ = nullptr;
        QComboBox *methodCombo_ = nullptr;
        QComboBox *shapeCombo_ = nullptr;
        QCheckBox *softEdgeCheck_ = nullptr;
        QToolButton *settingsButton_ = nullptr;
        QLineEdit *modelPathEdit_ = nullptr;
        QPushButton *downloadButton_ = nullptr;
        QLineEdit *outputDirEdit_ = nullptr;
        QListWidget *inputList_ = nullptr;
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
        QWidget *advancedBody_ = nullptr;

        QThread *workerThread_ = nullptr;
        ProcessorWorker *worker_ = nullptr;
        QElapsedTimer runTimer_;

        std::shared_ptr<YunetFaceDetector> cachedDetector_;
        std::shared_ptr<YunetFaceDetector> cachedVideoDetector_;
        QString cachedDetectorModelPath_;
        std::shared_ptr<PlateDetector> cachedPlateDetector_;
        QString cachedPlateModelPath_;

        QTranslator translator_;
        QTranslator qtTranslator_;
        QString language_;
        ThemeMode themeMode_ = ThemeMode::System;
        bool checkForUpdatesOnStartup_ = true;
        bool fileLogging_ = true;
        bool gpuAcceleration_ = true;
        int videoQuality_ = 0;
        bool shuttingDown_ = false;
        RunSummary lastRunSummary_;
        std::vector<std::function<void()>> retranslators_;
    };
}
