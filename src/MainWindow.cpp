#include "faceveil/MainWindow.hpp"

#include "faceveil/ProcessorWorker.hpp"
#include "faceveil/ReviewDialog.hpp"
#include "faceveil/ScrfdFaceDetector.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QCloseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolButton>
#include <QThread>
#include <QUrl>
#include <QWidget>

#include <array>

namespace faceveil
{
    namespace
    {
        constexpr const char *kStyleSheet = R"(
            QWidget {
                color: #111827;
                font-size: 13px;
            }
            QMainWindow, #rootScroll, #rootScroll > QWidget > QWidget {
                background-color: #F7F8FA;
            }
            QLabel#titleLabel {
                font-size: 22px;
                font-weight: 600;
                color: #111827;
            }
            QLabel#subtitleLabel {
                font-size: 12px;
                color: #6B7280;
            }
            QLabel[role="sectionTitle"] {
                font-size: 13px;
                font-weight: 600;
                color: #111827;
                letter-spacing: 0.2px;
            }
            QLabel[role="sectionHint"] {
                font-size: 12px;
                color: #6B7280;
            }
            QLabel[role="fieldLabel"] {
                font-size: 12px;
                color: #4B5563;
            }
            QFrame#card {
                background-color: #FFFFFF;
                border: 1px solid #E5E7EB;
                border-radius: 12px;
            }
            QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
                background-color: #FFFFFF;
                border: 1px solid #E5E7EB;
                border-radius: 8px;
                padding: 6px 10px;
                min-height: 20px;
                selection-background-color: #111827;
                selection-color: #FFFFFF;
            }
            QLineEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus {
                border: 1px solid #111827;
            }
            QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
                background-color: #F3F4F6;
                color: #9CA3AF;
            }
            QComboBox::drop-down {
                border: none;
                width: 22px;
            }
            QComboBox::down-arrow {
                image: none;
                border-left: 4px solid transparent;
                border-right: 4px solid transparent;
                border-top: 5px solid #6B7280;
                margin-right: 10px;
            }
            QPushButton {
                background-color: #FFFFFF;
                border: 1px solid #E5E7EB;
                border-radius: 8px;
                padding: 7px 14px;
                color: #111827;
                font-weight: 500;
            }
            QPushButton:hover {
                background-color: #F3F4F6;
                border-color: #D1D5DB;
            }
            QPushButton:pressed {
                background-color: #E5E7EB;
            }
            QPushButton:disabled {
                color: #9CA3AF;
                background-color: #F9FAFB;
                border-color: #E5E7EB;
            }
            QPushButton#primaryButton {
                background-color: #111827;
                color: #FFFFFF;
                border: 1px solid #111827;
                padding: 9px 20px;
                font-weight: 600;
            }
            QPushButton#primaryButton:hover {
                background-color: #1F2937;
            }
            QPushButton#primaryButton:pressed {
                background-color: #000000;
            }
            QPushButton#primaryButton:disabled {
                background-color: #9CA3AF;
                border-color: #9CA3AF;
                color: #FFFFFF;
            }
            QPushButton#dangerButton {
                background-color: #FFFFFF;
                color: #DC2626;
                border: 1px solid #FECACA;
            }
            QPushButton#dangerButton:hover {
                background-color: #FEF2F2;
            }
            QPushButton#dangerButton:disabled {
                color: #9CA3AF;
                border-color: #E5E7EB;
            }
            QListWidget, QPlainTextEdit {
                background-color: #FFFFFF;
                border: 1px solid #E5E7EB;
                border-radius: 8px;
                padding: 4px;
            }
            QListWidget::item {
                padding: 6px 8px;
                border-radius: 4px;
            }
            QListWidget::item:selected {
                background-color: #F3F4F6;
                color: #111827;
            }
            QPlainTextEdit {
                font-family: "SF Mono", "Menlo", "Consolas", monospace;
                font-size: 12px;
                color: #374151;
            }
            QCheckBox {
                spacing: 8px;
                color: #111827;
            }
            QProgressBar {
                background-color: #F3F4F6;
                border: none;
                border-radius: 4px;
                height: 8px;
                text-align: center;
                color: transparent;
            }
            QProgressBar::chunk {
                background-color: #111827;
                border-radius: 4px;
            }
            QScrollBar:vertical {
                background: transparent;
                width: 10px;
                margin: 2px;
            }
            QScrollBar::handle:vertical {
                background: #D1D5DB;
                border-radius: 4px;
                min-height: 24px;
            }
            QScrollBar::handle:vertical:hover {
                background: #9CA3AF;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0;
            }
            QScrollArea {
                border: none;
                background: transparent;
            }
            QWidget#bottomBar {
                background-color: #FFFFFF;
                border-top: 1px solid #E5E7EB;
            }
            QLabel#statusLabel {
                color: #6B7280;
                font-size: 12px;
            }
            QToolButton#advancedToggle {
                background: transparent;
                border: none;
                padding: 4px 2px;
                font-size: 13px;
                font-weight: 600;
                color: #111827;
            }
            QToolButton#advancedToggle:hover {
                color: #374151;
            }
        )";

        constexpr double kDefaultScoreThreshold = 0.5;
        constexpr double kDefaultNmsThreshold = 0.4;
        constexpr int kDefaultBlockSize = 28;
        constexpr double kDefaultPadding = 0.18;
        constexpr qint64 kMaxCustomModelBytes = 512LL * 1024LL * 1024LL;

        QString defaultOutputDirectory()
        {
            const auto pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            if (!pictures.isEmpty())
            {
                return pictures + "/FaceVeil";
            }
            return QDir::homePath() + "/FaceVeil";
        }

        QString firstExistingModelPath(const QString &fileName)
        {
            const auto appDir = QCoreApplication::applicationDirPath();
            const std::array<QString, 4> candidates = {
                appDir + "/models/" + fileName,
                appDir + "/../Resources/models/" + fileName,
                appDir + "/../../../../models/" + fileName,
                QDir::currentPath() + "/models/" + fileName,
            };

            for (const auto &candidate: candidates)
            {
                const QFileInfo info(QDir::cleanPath(candidate));
                if (info.exists() && info.isFile())
                {
                    return info.absoluteFilePath();
                }
            }

            return {};
        }

        QLabel *makeSectionTitle(const QString &text, QWidget *parent)
        {
            auto *label = new QLabel(text, parent);
            label->setProperty("role", "sectionTitle");
            return label;
        }

        QLabel *makeSectionHint(const QString &text, QWidget *parent)
        {
            auto *label = new QLabel(text, parent);
            label->setProperty("role", "sectionHint");
            label->setWordWrap(true);
            return label;
        }

        QLabel *makeFieldLabel(const QString &text, QWidget *parent)
        {
            auto *label = new QLabel(text, parent);
            label->setProperty("role", "fieldLabel");
            return label;
        }

        bool customModelFileIsAllowed(QWidget *parent, const QString &path)
        {
            const QFileInfo info(path);
            if (!info.exists() || !info.isFile())
            {
                QMessageBox::warning(parent, "Invalid Model", "Choose an existing ONNX model file.");
                return false;
            }
            if (info.suffix().compare("onnx", Qt::CaseInsensitive) != 0)
            {
                QMessageBox::warning(parent, "Invalid Model", "The selected model must use the .onnx extension.");
                return false;
            }
            if (info.size() > kMaxCustomModelBytes)
            {
                QMessageBox::warning(parent, "Model Too Large",
                                     "The selected ONNX file is larger than 512 MB. "
                                     "Choose a smaller SCRFD model.");
                return false;
            }
            return true;
        }

        bool confirmTrustedCustomModel(QWidget *parent, const QString &path)
        {
            const QFileInfo info(path);
            const auto answer = QMessageBox::question(
                parent,
                "Load Custom Model",
                QString("Only load ONNX models from sources you trust.\n\nModel: %1\nSize: %2 MB\n\nContinue?")
                    .arg(info.fileName())
                    .arg(QString::number(info.size() / 1024.0 / 1024.0, 'f', 1)),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No);
            return answer == QMessageBox::Yes;
        }

        QFrame *makeCard(QWidget *parent)
        {
            auto *card = new QFrame(parent);
            card->setObjectName("card");
            card->setFrameShape(QFrame::NoFrame);
            return card;
        }
    }

    MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
    {
        setWindowTitle("FaceVeil");
        setAcceptDrops(true);
        resize(920, 760);
        setMinimumSize(720, 600);
        setStyleSheet(kStyleSheet);

        auto *container = new QWidget(this);
        auto *containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);

        auto *scrollArea = new QScrollArea(container);
        scrollArea->setObjectName("rootScroll");
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setFrameShape(QFrame::NoFrame);

        auto *central = new QWidget(scrollArea);
        auto *root = new QVBoxLayout(central);
        root->setContentsMargins(32, 28, 32, 28);
        root->setSpacing(20);

        auto *header = new QWidget(central);
        auto *headerLayout = new QVBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(4);

        auto *title = new QLabel("FaceVeil", header);
        title->setObjectName("titleLabel");
        auto *subtitle = new QLabel("Local, private face anonymization for photos", header);
        subtitle->setObjectName("subtitleLabel");
        headerLayout->addWidget(title);
        headerLayout->addWidget(subtitle);
        root->addWidget(header);

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(12);

            cardLayout->addWidget(makeSectionTitle("Model", card));
            cardLayout->addWidget(makeSectionHint(
                "Choose speed vs. accuracy, or load a custom SCRFD ONNX file.", card));

            modelCombo_ = new QComboBox(card);
            modelCombo_->setMinimumHeight(34);
            cardLayout->addWidget(modelCombo_);

            auto *pathRow = new QHBoxLayout();
            pathRow->setSpacing(8);
            modelPathEdit_ = new QLineEdit(card);
            modelPathEdit_->setReadOnly(true);
            modelPathEdit_->setPlaceholderText("Bundled SCRFD model path");
            auto *modelButton = new QPushButton("Browse…", card);
            modelButton->setCursor(Qt::PointingHandCursor);
            pathRow->addWidget(modelPathEdit_, 1);
            pathRow->addWidget(modelButton);
            cardLayout->addLayout(pathRow);

            connect(modelButton, &QPushButton::clicked, this, &MainWindow::chooseModel);
            connect(modelCombo_, &QComboBox::currentIndexChanged,
                    this, &MainWindow::updateModelPathFromSelection);

            root->addWidget(card);
        }

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(12);

            cardLayout->addWidget(makeSectionTitle("Inputs", card));
            cardLayout->addWidget(makeSectionHint(
                "Drag images or folders here, or use the buttons below.", card));

            inputList_ = new QListWidget(card);
            inputList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
            inputList_->setMinimumHeight(140);
            inputList_->setAlternatingRowColors(false);
            cardLayout->addWidget(inputList_);

            auto *buttonRow = new QHBoxLayout();
            buttonRow->setSpacing(8);
            auto *addFiles = new QPushButton("Add Images", card);
            auto *addFolder = new QPushButton("Add Folder", card);
            auto *clearInputs = new QPushButton("Clear", card);
            addFiles->setCursor(Qt::PointingHandCursor);
            addFolder->setCursor(Qt::PointingHandCursor);
            clearInputs->setCursor(Qt::PointingHandCursor);
            recursiveCheck_ = new QCheckBox("Include subfolders", card);
            recursiveCheck_->setChecked(true);
            reviewCheck_ = new QCheckBox("Review each image", card);
            reviewCheck_->setChecked(false);
            reviewCheck_->setToolTip(
                "Before saving each image, open a preview where you can:\n"
                "  • Click a detected box to exclude it\n"
                "  • Drag an empty area to add a box the model missed");

            connect(addFiles, &QPushButton::clicked, this, &MainWindow::chooseFiles);
            connect(addFolder, &QPushButton::clicked, this, &MainWindow::chooseFolder);
            connect(clearInputs, &QPushButton::clicked, inputList_, &QListWidget::clear);

            buttonRow->addWidget(addFiles);
            buttonRow->addWidget(addFolder);
            buttonRow->addWidget(clearInputs);
            buttonRow->addStretch(1);
            buttonRow->addWidget(reviewCheck_);
            buttonRow->addWidget(recursiveCheck_);
            cardLayout->addLayout(buttonRow);

            root->addWidget(card);
        }

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(12);

            cardLayout->addWidget(makeSectionTitle("Output", card));
            cardLayout->addWidget(makeSectionHint(
                "Anonymized copies are written here, preserving folder structure.", card));

            auto *outputRow = new QHBoxLayout();
            outputRow->setSpacing(8);
            outputDirEdit_ = new QLineEdit(defaultOutputDirectory(), card);
            auto *outputButton = new QPushButton("Choose…", card);
            outputButton->setCursor(Qt::PointingHandCursor);
            outputRow->addWidget(outputDirEdit_, 1);
            outputRow->addWidget(outputButton);
            cardLayout->addLayout(outputRow);
            connect(outputButton, &QPushButton::clicked, this, &MainWindow::chooseOutputDirectory);

            root->addWidget(card);
        }

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 14, 20, 14);
            cardLayout->setSpacing(0);

            auto *headerRow = new QHBoxLayout();
            headerRow->setSpacing(8);
            headerRow->setContentsMargins(0, 0, 0, 0);

            advancedToggle_ = new QToolButton(card);
            advancedToggle_->setObjectName("advancedToggle");
            advancedToggle_->setText("Advanced Options");
            advancedToggle_->setCheckable(true);
            advancedToggle_->setChecked(false);
            advancedToggle_->setArrowType(Qt::RightArrow);
            advancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            advancedToggle_->setCursor(Qt::PointingHandCursor);
            advancedToggle_->setFocusPolicy(Qt::NoFocus);

            auto *resetButton = new QPushButton("Reset to defaults", card);
            resetButton->setCursor(Qt::PointingHandCursor);

            headerRow->addWidget(advancedToggle_);
            headerRow->addStretch(1);
            headerRow->addWidget(resetButton);
            cardLayout->addLayout(headerRow);

            advancedBody_ = new QWidget(card);
            auto *bodyLayout = new QVBoxLayout(advancedBody_);
            bodyLayout->setContentsMargins(0, 14, 0, 4);
            bodyLayout->setSpacing(12);

            bodyLayout->addWidget(makeSectionHint(
                "Tweak detection and mosaic behavior. Defaults work for most photos.",
                advancedBody_));

            methodCombo_ = new QComboBox(advancedBody_);
            methodCombo_->addItem("Mosaic (pixelate)", static_cast<int>(AnonymizationMethod::Mosaic));
            methodCombo_->addItem("Gaussian blur", static_cast<int>(AnonymizationMethod::Blur));
            methodCombo_->addItem("Solid fill (blackout)", static_cast<int>(AnonymizationMethod::Fill));
            methodCombo_->setToolTip(
                "How detected faces are obscured.\n"
                "Mosaic = pixelation (block size below).\n"
                "Gaussian blur = strong smoothing scaled to face size.\n"
                "Solid fill = opaque black box, irreversible. Default: Mosaic");

            scoreThresholdSpin_ = new QDoubleSpinBox(advancedBody_);
            scoreThresholdSpin_->setRange(0.05, 0.99);
            scoreThresholdSpin_->setSingleStep(0.05);
            scoreThresholdSpin_->setDecimals(2);
            scoreThresholdSpin_->setValue(kDefaultScoreThreshold);
            scoreThresholdSpin_->setToolTip(
                "Minimum confidence to accept a face.\n"
                "Higher = fewer false positives but may miss small or side-profile faces.\n"
                "Lower = catches more faces but may blur non-face regions. Default: 0.50");

            nmsThresholdSpin_ = new QDoubleSpinBox(advancedBody_);
            nmsThresholdSpin_->setRange(0.05, 0.95);
            nmsThresholdSpin_->setSingleStep(0.05);
            nmsThresholdSpin_->setDecimals(2);
            nmsThresholdSpin_->setValue(kDefaultNmsThreshold);
            nmsThresholdSpin_->setToolTip(
                "Non-Maximum Suppression overlap threshold for duplicate boxes.\n"
                "Lower = more aggressively removes overlapping detections.\n"
                "Higher = allows more overlap. Default: 0.40");

            blockSizeSpin_ = new QSpinBox(advancedBody_);
            blockSizeSpin_->setRange(2, 200);
            blockSizeSpin_->setValue(kDefaultBlockSize);
            blockSizeSpin_->setToolTip(
                "Mosaic block size in pixels.\n"
                "Larger = coarser blocks, harder to un-blur.\n"
                "Smaller = finer mosaic, higher recovery risk. Default: 28");

            paddingSpin_ = new QDoubleSpinBox(advancedBody_);
            paddingSpin_->setRange(0.0, 1.0);
            paddingSpin_->setSingleStep(0.05);
            paddingSpin_->setDecimals(2);
            paddingSpin_->setValue(kDefaultPadding);
            paddingSpin_->setToolTip(
                "Extra margin around each detected face, as a fraction of its size.\n"
                "Covers ears, hairline, and chin that the detector may miss.\n"
                "0.00 = exact box, 0.18 = ~18% larger. Default: 0.18");

            auto *grid = new QFormLayout();
            grid->setLabelAlignment(Qt::AlignLeft);
            grid->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
            grid->setHorizontalSpacing(18);
            grid->setVerticalSpacing(10);
            grid->addRow(makeFieldLabel("Anonymization", advancedBody_), methodCombo_);
            grid->addRow(makeFieldLabel("Score threshold", advancedBody_), scoreThresholdSpin_);
            grid->addRow(makeFieldLabel("NMS threshold", advancedBody_), nmsThresholdSpin_);
            grid->addRow(makeFieldLabel("Mosaic block size", advancedBody_), blockSizeSpin_);
            grid->addRow(makeFieldLabel("Face padding", advancedBody_), paddingSpin_);
            bodyLayout->addLayout(grid);

            advancedBody_->setVisible(false);
            cardLayout->addWidget(advancedBody_);

            connect(advancedToggle_, &QToolButton::toggled, this, &MainWindow::toggleAdvanced);
            connect(resetButton, &QPushButton::clicked, this, &MainWindow::resetAdvancedDefaults);

            root->addWidget(card);
        }

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(10);

            cardLayout->addWidget(makeSectionTitle("Activity", card));
            logEdit_ = new QPlainTextEdit(card);
            logEdit_->setReadOnly(true);
            logEdit_->setMinimumHeight(140);

            logEdit_->setMaximumBlockCount(5000);
            cardLayout->addWidget(logEdit_);
            root->addWidget(card);
        }

        scrollArea->setWidget(central);
        containerLayout->addWidget(scrollArea, 1);

        auto *bottomBar = new QWidget(container);
        bottomBar->setObjectName("bottomBar");
        bottomBar->setAutoFillBackground(true);
        auto *bottomLayout = new QHBoxLayout(bottomBar);
        bottomLayout->setContentsMargins(24, 14, 24, 14);
        bottomLayout->setSpacing(14);

        statusLabel_ = new QLabel("Ready", bottomBar);
        statusLabel_->setObjectName("statusLabel");

        progressBar_ = new QProgressBar(bottomBar);
        progressBar_->setRange(0, 100);
        progressBar_->setValue(0);
        progressBar_->setTextVisible(false);
        progressBar_->setFixedHeight(6);

        stopButton_ = new QPushButton("Stop", bottomBar);
        stopButton_->setObjectName("dangerButton");
        stopButton_->setCursor(Qt::PointingHandCursor);
        stopButton_->setEnabled(false);

        startButton_ = new QPushButton("Start", bottomBar);
        startButton_->setObjectName("primaryButton");
        startButton_->setCursor(Qt::PointingHandCursor);
        startButton_->setDefault(true);

        connect(startButton_, &QPushButton::clicked, this, &MainWindow::startProcessing);
        connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopProcessing);

        bottomLayout->addWidget(statusLabel_);
        bottomLayout->addWidget(progressBar_, 1);
        bottomLayout->addWidget(stopButton_);
        bottomLayout->addWidget(startButton_);

        containerLayout->addWidget(bottomBar);

        setCentralWidget(container);
        statusBar()->hide();

        populateBundledModels();
        loadSettings();
        appendLog("Ready. Drop images or folders to begin.");
    }

    MainWindow::~MainWindow()
    {
        if (workerThread_ != nullptr)
        {
            stopProcessing();
            workerThread_->quit();
            workerThread_->wait();
        }
    }

    namespace
    {
        bool hasAcceptableLocalUrls(const QMimeData *mime)
        {
            if (mime == nullptr || !mime->hasUrls())
            {
                return false;
            }
            for (const auto &url: mime->urls())
            {
                if (!url.isLocalFile())
                {
                    continue;
                }
                const QFileInfo info(url.toLocalFile());
                if (info.exists() && (info.isFile() || info.isDir()))
                {
                    return true;
                }
            }
            return false;
        }
    }

    void MainWindow::dragEnterEvent(QDragEnterEvent *event)
    {
        if (hasAcceptableLocalUrls(event->mimeData()))
        {
            event->acceptProposedAction();
        } else
        {
            event->ignore();
        }
    }

    void MainWindow::dropEvent(QDropEvent *event)
    {
        if (!hasAcceptableLocalUrls(event->mimeData()))
        {
            event->ignore();
            return;
        }
        for (const auto &url: event->mimeData()->urls())
        {
            if (!url.isLocalFile())
            {
                continue;
            }
            const QFileInfo info(url.toLocalFile());
            if (info.exists() && (info.isFile() || info.isDir()))
            {
                addInputPath(url.toLocalFile());
            }
        }
        event->acceptProposedAction();
    }

    void MainWindow::chooseModel()
    {
        const auto path = QFileDialog::getOpenFileName(this, "Select SCRFD ONNX Model", QDir::currentPath(),
                                                       "ONNX Models (*.onnx)");
        if (!path.isEmpty())
        {
            if (!customModelFileIsAllowed(this, path) || !confirmTrustedCustomModel(this, path))
            {
                return;
            }
            const QFileInfo info(path);
            modelCombo_->addItem("Custom — " + info.fileName(), info.absoluteFilePath());
            modelCombo_->setCurrentIndex(modelCombo_->count() - 1);
            modelPathEdit_->setText(path);
        }
    }

    void MainWindow::chooseFiles()
    {
        const auto files = QFileDialog::getOpenFileNames(this,
                                                         "Select Images",
                                                         QStandardPaths::writableLocation(
                                                             QStandardPaths::PicturesLocation),
                                                         "Images (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp)");
        for (const auto &file: files)
        {
            addInputPath(file);
        }
    }

    void MainWindow::chooseFolder()
    {
        const auto folder = QFileDialog::getExistingDirectory(this, "Select Folder",
                                                              QStandardPaths::writableLocation(
                                                                  QStandardPaths::PicturesLocation));
        if (!folder.isEmpty())
        {
            addInputPath(folder);
        }
    }

    void MainWindow::chooseOutputDirectory()
    {
        const auto folder = QFileDialog::getExistingDirectory(this, "Select Output Folder", outputDirEdit_->text());
        if (!folder.isEmpty())
        {
            outputDirEdit_->setText(folder);
        }
    }

    void MainWindow::startProcessing()
    {
        const auto modelPath = selectedModelPath();
        if (modelPath.isEmpty() || !QFileInfo::exists(modelPath))
        {
            appendLog("Choose a valid SCRFD ONNX model first.");
            return;
        }
        const auto currentLabel = modelCombo_ ? modelCombo_->currentText() : QString();
        if (currentLabel.startsWith("Custom") && !customModelFileIsAllowed(this, modelPath))
        {
            return;
        }

        if (inputList_->count() == 0)
        {
            appendLog("Add at least one image or folder.");
            return;
        }

        if (outputDirEdit_->text().isEmpty())
        {
            appendLog("Choose an output folder.");
            return;
        }

        const QString rawOutput = outputDirEdit_->text();
        const QFileInfo outputInfo(rawOutput);
        const QString canonicalOutput = outputInfo.exists()
                                            ? outputInfo.canonicalFilePath()
                                            : QDir::cleanPath(rawOutput);
        for (const auto &input: inputPaths())
        {
            const QFileInfo inputInfo(input);
            const QString canonicalInput = inputInfo.exists()
                                               ? inputInfo.canonicalFilePath()
                                               : QDir::cleanPath(input);
            if (canonicalInput.isEmpty() || canonicalOutput.isEmpty())
            {
                continue;
            }

            const bool isSame = canonicalInput.compare(canonicalOutput, Qt::CaseInsensitive) == 0;
            const QString withSep = canonicalInput.endsWith('/') ? canonicalInput : canonicalInput + '/';
            const bool isUnder = canonicalOutput.startsWith(withSep, Qt::CaseInsensitive);
            if (isSame || isUnder)
            {
                appendLog(QString("Refusing to run: output folder is inside input '%1'. "
                        "Pick a different output folder so originals aren't overwritten.")
                    .arg(input));
                return;
            }
        }

        setProcessing(true);
        progressBar_->setValue(0);
        statusLabel_->setText("Starting…");

        auto detectorForRun = (cachedDetectorModelPath_ == modelPath) ? cachedDetector_ : nullptr;

        workerThread_ = new QThread(this);
        worker_ = new ProcessorWorker(modelPath,
                                      inputPaths(),
                                      outputDirEdit_->text(),
                                      recursiveCheck_->isChecked(),
                                      static_cast<float>(scoreThresholdSpin_->value()),
                                      static_cast<float>(nmsThresholdSpin_->value()),
                                      blockSizeSpin_->value(),
                                      static_cast<float>(paddingSpin_->value()),
                                      static_cast<AnonymizationMethod>(methodCombo_->currentData().toInt()),
                                      reviewCheck_->isChecked(),
                                      this,
                                      std::move(detectorForRun));

        worker_->moveToThread(workerThread_);
        connect(workerThread_, &QThread::started, worker_, &ProcessorWorker::process);
        connect(worker_, &ProcessorWorker::logMessage, this, &MainWindow::appendLog);
        connect(worker_, &ProcessorWorker::progressChanged, this, [this](int completed, int total)
        {
            progressBar_->setRange(0, std::max(total, 1));
            progressBar_->setValue(completed);
        });
        connect(worker_, &ProcessorWorker::stageChanged, this,
                [this](int index, int total, const QString &stage, const QString &fileName)
                {
                    statusLabel_->setText(QString("%1/%2  ·  %3  ·  %4")
                        .arg(index).arg(total).arg(stage, fileName));
                });

        connect(worker_, &ProcessorWorker::finished, this, &MainWindow::onWorkerFinished);
        connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
        connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);

        appendLog("Starting…");
        workerThread_->start();
    }

    void MainWindow::stopProcessing() const
    {
        if (worker_ != nullptr)
        {
            appendLog("Stopping after the current processing step…");
            statusLabel_->setText("Stopping…");
            QMetaObject::invokeMethod(worker_, "cancel", Qt::QueuedConnection);
        }
    }

    void MainWindow::onWorkerFinished(bool cancelled)
    {
        appendLog(cancelled ? "Cancelled." : "Finished.");
        statusLabel_->setText(cancelled ? "Cancelled" : "Done");
        setProcessing(false);

        if (worker_ != nullptr)
        {
            auto detector = worker_->takeDetector();
            if (detector)
            {
                cachedDetector_ = std::move(detector);
                cachedDetectorModelPath_ = selectedModelPath();
            }
        }

        if (workerThread_ != nullptr)
        {
            workerThread_->quit();
        }

        worker_ = nullptr;
        workerThread_ = nullptr;
    }

    void MainWindow::toggleAdvanced(bool expanded) const
    {
        if (advancedBody_ != nullptr)
        {
            advancedBody_->setVisible(expanded);
        }
        if (advancedToggle_ != nullptr)
        {
            advancedToggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        }
    }

    void MainWindow::resetAdvancedDefaults() const
    {
        scoreThresholdSpin_->setValue(kDefaultScoreThreshold);
        nmsThresholdSpin_->setValue(kDefaultNmsThreshold);
        blockSizeSpin_->setValue(kDefaultBlockSize);
        paddingSpin_->setValue(kDefaultPadding);
        methodCombo_->setCurrentIndex(0);
    }

    void MainWindow::closeEvent(QCloseEvent *event)
    {
        saveSettings();
        QMainWindow::closeEvent(event);
    }

    void MainWindow::loadSettings()
    {
        QSettings settings;

        settings.beginGroup("window");
        const auto geometry = settings.value("geometry").toByteArray();
        if (!geometry.isEmpty())
        {
            restoreGeometry(geometry);
        }
        settings.endGroup();

        settings.beginGroup("processing");
        const auto savedModelIndex = settings.value("modelIndex", 0).toInt();
        if (modelCombo_ != nullptr && savedModelIndex >= 0 && savedModelIndex < modelCombo_->count())
        {
            modelCombo_->setCurrentIndex(savedModelIndex);
        }
        const auto savedCustomModel = settings.value("customModelPath").toString();
        if (!savedCustomModel.isEmpty() && QFileInfo::exists(savedCustomModel))
        {
            const QFileInfo info(savedCustomModel);
            modelCombo_->addItem("Custom — " + info.fileName(), info.absoluteFilePath());
            modelCombo_->setCurrentIndex(modelCombo_->count() - 1);
        }

        const auto outputDir = settings.value("outputDir").toString();
        if (!outputDir.isEmpty())
        {
            outputDirEdit_->setText(outputDir);
        }

        recursiveCheck_->setChecked(settings.value("recursive", true).toBool());
        reviewCheck_->setChecked(settings.value("review", false).toBool());

        scoreThresholdSpin_->setValue(settings.value("scoreThreshold", kDefaultScoreThreshold).toDouble());
        nmsThresholdSpin_->setValue(settings.value("nmsThreshold", kDefaultNmsThreshold).toDouble());
        blockSizeSpin_->setValue(settings.value("blockSize", kDefaultBlockSize).toInt());
        paddingSpin_->setValue(settings.value("padding", kDefaultPadding).toDouble());

        const auto savedMethod = settings.value("method", 0).toInt();
        if (methodCombo_ != nullptr && savedMethod >= 0 && savedMethod < methodCombo_->count())
        {
            methodCombo_->setCurrentIndex(savedMethod);
        }

        if (settings.value("advancedExpanded", false).toBool())
        {
            advancedToggle_->setChecked(true);
        }
        settings.endGroup();

        updateModelPathFromSelection();
    }

    void MainWindow::saveSettings() const
    {
        QSettings settings;

        settings.beginGroup("window");
        settings.setValue("geometry", saveGeometry());
        settings.endGroup();

        settings.beginGroup("processing");
        settings.setValue("modelIndex", modelCombo_ ? modelCombo_->currentIndex() : 0);

        const auto currentLabel = modelCombo_ ? modelCombo_->currentText() : QString();
        if (currentLabel.startsWith("Custom"))
        {
            settings.setValue("customModelPath", selectedModelPath());
        } else
        {
            settings.remove("customModelPath");
        }

        settings.setValue("outputDir", outputDirEdit_->text());
        settings.setValue("recursive", recursiveCheck_->isChecked());
        settings.setValue("review", reviewCheck_->isChecked());
        settings.setValue("scoreThreshold", scoreThresholdSpin_->value());
        settings.setValue("nmsThreshold", nmsThresholdSpin_->value());
        settings.setValue("blockSize", blockSizeSpin_->value());
        settings.setValue("padding", paddingSpin_->value());
        settings.setValue("method", methodCombo_ ? methodCombo_->currentIndex() : 0);
        settings.setValue("advancedExpanded", advancedToggle_ ? advancedToggle_->isChecked() : false);
        settings.endGroup();
    }

    void MainWindow::addInputPath(const QString &path) const
    {
        if (path.isEmpty())
        {
            return;
        }

        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        const QString key = canonical.isEmpty() ? QDir::cleanPath(path) : canonical;

        for (int i = 0; i < inputList_->count(); ++i)
        {
            const QString existing = inputList_->item(i)->text();
            const QFileInfo existingInfo(existing);
            const QString existingKey = existingInfo.canonicalFilePath().isEmpty()
                                            ? QDir::cleanPath(existing)
                                            : existingInfo.canonicalFilePath();
            if (existingKey.compare(key, Qt::CaseInsensitive) == 0)
            {
                return;
            }
        }
        inputList_->addItem(path);
    }

    void MainWindow::setProcessing(bool processing) const
    {
        startButton_->setEnabled(!processing);
        stopButton_->setEnabled(processing);
        modelCombo_->setEnabled(!processing);
        methodCombo_->setEnabled(!processing);
        modelPathEdit_->setEnabled(!processing);
        outputDirEdit_->setEnabled(!processing);
        inputList_->setEnabled(!processing);
        recursiveCheck_->setEnabled(!processing);
        reviewCheck_->setEnabled(!processing);
        scoreThresholdSpin_->setEnabled(!processing);
        nmsThresholdSpin_->setEnabled(!processing);
        blockSizeSpin_->setEnabled(!processing);
        paddingSpin_->setEnabled(!processing);
    }

    QStringList MainWindow::inputPaths() const
    {
        QStringList paths;
        for (int i = 0; i < inputList_->count(); ++i)
        {
            paths.append(inputList_->item(i)->text());
        }
        return paths;
    }

    void MainWindow::populateBundledModels() const
    {
        struct ModelOption
        {
            QString label;
            QString fileName;
        };

        const std::array<ModelOption, 2> options = {
            ModelOption{"Fast  ·  SCRFD 2.5G", "2.5g_bnkps.onnx"},
            ModelOption{"Accurate  ·  SCRFD 10G", "10g_bnkps.onnx"},
        };

        for (const auto &option: options)
        {
            const auto path = firstExistingModelPath(option.fileName);
            modelCombo_->addItem(option.label, path);
        }

        modelCombo_->setCurrentIndex(0);
        updateModelPathFromSelection();

        if (selectedModelPath().isEmpty())
        {
            appendLog("Bundled models not found. Use Browse… to select an ONNX file.");
        }
    }

    void MainWindow::updateModelPathFromSelection() const
    {
        modelPathEdit_->setText(selectedModelPath());
    }

    QString MainWindow::selectedModelPath() const
    {
        if (modelCombo_ == nullptr || modelCombo_->currentIndex() < 0)
        {
            return {};
        }

        return modelCombo_->currentData().toString();
    }

    void MainWindow::appendLog(const QString &message) const
    {
        const auto time = QDateTime::currentDateTime().toString("HH:mm:ss");
        logEdit_->appendPlainText(QString("[%1]  %2").arg(time, message));
    }

    ReviewResult MainWindow::requestReview(const QImage &image,
                                           const QString &sourceName,
                                           const QVector<QRectF> &detected,
                                           int currentIndex,
                                           int total)
    {
        ReviewDialog dialog(image, sourceName, detected, currentIndex, total, this);
        dialog.exec();
        return dialog.result();
    }
}
