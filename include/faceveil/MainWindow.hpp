#pragma once

#include "faceveil/ReviewTypes.hpp"

#include <QMainWindow>
#include <QVector>

#include <memory>

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

namespace faceveil
{
    class ProcessorWorker;
    class ScrfdFaceDetector;

    class MainWindow final : public QMainWindow
    {
        Q_OBJECT

    public:
        explicit MainWindow(QWidget *parent = nullptr);

        ~MainWindow() override;

        Q_INVOKABLE faceveil::ReviewResult requestReview(const QImage &image,
                                                         const QString &sourceName,
                                                         const QVector<QRectF> &detected,
                                                         int currentIndex,
                                                         int total);

    protected:
        void dragEnterEvent(QDragEnterEvent *event) override;

        void dropEvent(QDropEvent *event) override;

        void closeEvent(QCloseEvent *event) override;

    private slots:
        void chooseModel();

        void chooseFiles();

        void chooseFolder();

        void chooseOutputDirectory();

        void startProcessing();

        void stopProcessing() const;

        void onWorkerFinished(bool cancelled);

        void toggleAdvanced(bool expanded) const;

        void resetAdvancedDefaults() const;

    private:
        void addInputPath(const QString &path) const;

        void populateBundledModels() const;

        void updateModelPathFromSelection() const;

        [[nodiscard]] QString selectedModelPath() const;

        void setProcessing(bool processing) const;

        [[nodiscard]] QStringList inputPaths() const;

        void appendLog(const QString &message) const;

        void loadSettings();

        void saveSettings() const;

        QComboBox *modelCombo_ = nullptr;
        QComboBox *methodCombo_ = nullptr;
        QLineEdit *modelPathEdit_ = nullptr;
        QLineEdit *outputDirEdit_ = nullptr;
        QListWidget *inputList_ = nullptr;
        QCheckBox *recursiveCheck_ = nullptr;
        QCheckBox *reviewCheck_ = nullptr;
        QDoubleSpinBox *scoreThresholdSpin_ = nullptr;
        QDoubleSpinBox *nmsThresholdSpin_ = nullptr;
        QSpinBox *blockSizeSpin_ = nullptr;
        QDoubleSpinBox *paddingSpin_ = nullptr;
        QProgressBar *progressBar_ = nullptr;
        QPlainTextEdit *logEdit_ = nullptr;
        QPushButton *startButton_ = nullptr;
        QPushButton *stopButton_ = nullptr;
        QLabel *statusLabel_ = nullptr;
        QToolButton *advancedToggle_ = nullptr;
        QWidget *advancedBody_ = nullptr;

        QThread *workerThread_ = nullptr;
        ProcessorWorker *worker_ = nullptr;

        std::shared_ptr<ScrfdFaceDetector> cachedDetector_;
        QString cachedDetectorModelPath_;
    };
}
