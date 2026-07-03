#include "redactly/MainWindow.hpp"

#include "redactly/ImageScanner.hpp"
#include "redactly/ModelCatalog.hpp"
#include "redactly/ModelDownloader.hpp"
#include "redactly/Mosaic.hpp"
#include "redactly/PathUtil.hpp"
#include "redactly/PlateDetector.hpp"
#include "redactly/ProcessorWorker.hpp"
#include "redactly/ReviewDialog.hpp"
#include "redactly/ScrfdFaceDetector.hpp"
#include "redactly/SettingsDialog.hpp"
#include "redactly/Theme.hpp"
#include "redactly/UpdateChecker.hpp"
#include "redactly/VideoIo.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QCloseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolButton>
#include <QThread>
#include <QUrl>
#include <QWidget>

#include <QDir>
#include <QEventLoop>
#include <QImageReader>
#include <QLibraryInfo>
#include <QPainter>
#include <QPaintEvent>
#include <QProcess>
#include <QProgressDialog>
#include <QScreen>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace redactly
{
    namespace
    {
        constexpr double kDefaultScoreThreshold = 0.5;
        constexpr double kDefaultNmsThreshold = 0.4;
        constexpr int kDefaultBlockSize = 14;
        constexpr double kDefaultPadding = 0.18;

        QString defaultOutputDirectory()
        {
            const auto pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            if (!pictures.isEmpty())
            {
                return pictures + "/Redactly";
            }
            return QDir::homePath() + "/Redactly";
        }

        QLabel *makeSectionTitle(QWidget *parent)
        {
            auto *label = new QLabel(parent);
            label->setProperty("role", "sectionTitle");
            return label;
        }

        QLabel *makeSectionHint(QWidget *parent)
        {
            auto *label = new QLabel(parent);
            label->setProperty("role", "sectionHint");
            label->setWordWrap(true);
            return label;
        }

        QLabel *makeFieldLabel(QWidget *parent)
        {
            auto *label = new QLabel(parent);
            label->setProperty("role", "fieldLabel");
            return label;
        }

        QIcon videoThumbnailIcon(const QString &path)
        {
            const auto tools = locateFfmpegTools();
            if (!tools)
            {
                return {};
            }
            QProcess process;
            process.start(tools->ffmpegPath,
                          {"-v", "error", "-ss", "0", "-i", path,
                           "-frames:v", "1", "-vf", "scale=80:-2",
                           "-f", "image2pipe", "-c:v", "png", "-"});
            if (!process.waitForStarted(3000) || !process.waitForFinished(3000))
            {
                process.kill();
                return {};
            }
            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            {
                return {};
            }
            QImage image;
            if (!image.loadFromData(process.readAllStandardOutput(), "PNG") || image.isNull())
            {
                return {};
            }
            return QIcon(QPixmap::fromImage(image));
        }

        QFrame *makeCard(QWidget *parent)
        {
            auto *card = new QFrame(parent);
            card->setObjectName("card");
            card->setFrameShape(QFrame::NoFrame);
            return card;
        }

        QPixmap anonymizationSamplePixmap(AnonymizationMethod method, MaskShape shape,
                                          int blockSize, float padding, bool softEdges)
        {
            qreal dpr = 1.0;
            for (const QScreen *screen: QGuiApplication::screens())
            {
                dpr = std::max(dpr, screen->devicePixelRatio());
            }
            const int w = static_cast<int>(std::lround(152 * dpr));
            const int h = static_cast<int>(std::lround(96 * dpr));

            cv::Mat sample(h, w, CV_8UC3);
            const int cell = std::max(2, static_cast<int>(std::lround(6 * dpr)));
            for (int y = 0; y < h; ++y)
            {
                for (int x = 0; x < w; ++x)
                {
                    const bool checker = ((x / cell) + (y / cell)) % 2 == 0;
                    const int base = checker ? 205 : 150;
                    sample.at<cv::Vec3b>(y, x) = cv::Vec3b(
                        static_cast<uchar>(std::clamp(base - (y * 60) / h, 0, 255)),
                        static_cast<uchar>(std::clamp(base - 40 + (x * 70) / w, 0, 255)),
                        static_cast<uchar>(std::clamp(base + (y * 40) / h - 20, 0, 255)));
                }
            }

            FaceDetections detections;
            detections.push_back({cv::Rect2f(w * 0.3F, h * 0.2F, w * 0.4F, h * 0.6F), 1.0F});
            applyAnonymization(sample, detections, method,
                               std::max(2, static_cast<int>(std::lround(blockSize * dpr))),
                               padding, shape, softEdges);

            const QImage image(sample.data, sample.cols, sample.rows,
                               static_cast<int>(sample.step), QImage::Format_BGR888);
            QPixmap pixmap = QPixmap::fromImage(image.copy());
            pixmap.setDevicePixelRatio(dpr);
            return pixmap;
        }

        class DropListWidget final : public QListWidget
        {
        public:
            using QListWidget::QListWidget;

            void setPlaceholderText(const QString &text)
            {
                placeholder_ = text;
                viewport()->update();
            }

            void removeSelectedItems()
            {
                const auto items = selectedItems();
                for (auto *item: items)
                {
                    delete item;
                }
            }

        protected:
            void paintEvent(QPaintEvent *event) override
            {
                QListWidget::paintEvent(event);
                if (count() != 0 || placeholder_.isEmpty())
                {
                    return;
                }
                QPainter painter(viewport());
                painter.setPen(palette().placeholderText().color());
                painter.drawText(viewport()->rect().adjusted(24, 0, -24, 0),
                                 Qt::AlignCenter | Qt::TextWordWrap, placeholder_);
            }

            void keyPressEvent(QKeyEvent *event) override
            {
                if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
                {
                    removeSelectedItems();
                    return;
                }
                QListWidget::keyPressEvent(event);
            }

            void contextMenuEvent(QContextMenuEvent *event) override
            {
                QMenu menu(this);
                QAction *removeAction = menu.addAction(
                    QCoreApplication::translate("redactly::MainWindow", "Remove Selected"));
                removeAction->setEnabled(!selectedItems().isEmpty());
                QAction *clearAction = menu.addAction(
                    QCoreApplication::translate("redactly::MainWindow", "Clear All"));
                clearAction->setEnabled(count() > 0);
                QAction *chosen = menu.exec(event->globalPos());
                if (chosen == removeAction)
                {
                    removeSelectedItems();
                }
                else if (chosen == clearAction)
                {
                    clear();
                }
            }

        private:
            QString placeholder_;
        };
    }

    MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
    {
        setWindowTitle("Redactly");
        setAcceptDrops(true);
        resize(920, 760);
        setMinimumSize(720, 600);

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

        auto *titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(8);
        auto *title = new QLabel("Redactly", header);
        title->setObjectName("titleLabel");

        settingsButton_ = new QToolButton(header);
        settingsButton_->setObjectName("settingsButton");
        settingsButton_->setFixedSize(36, 36);
        settingsButton_->setCursor(Qt::PointingHandCursor);
        updateSettingsIcon();
        addRetranslation([this]
                         {
                             settingsButton_->setToolTip(tr("Settings"));
                             settingsButton_->setAccessibleName(tr("Settings"));
                         });
        connect(settingsButton_, &QToolButton::clicked, this, &MainWindow::openSettings);
        auto *settingsShortcut = new QShortcut(QKeySequence::Preferences, this);
        connect(settingsShortcut, &QShortcut::activated, this, &MainWindow::openSettings);

        auto *versionLabel = new QLabel(QString("v%1").arg(QCoreApplication::applicationVersion()), header);
        versionLabel->setObjectName("subtitleLabel");

        updateLabel_ = new QLabel(header);
        updateLabel_->setObjectName("subtitleLabel");
        updateLabel_->setTextFormat(Qt::RichText);
        updateLabel_->setOpenExternalLinks(true);
        updateLabel_->setVisible(false);

        auto *titleGroup = new QWidget(header);
        auto *titleGroupLayout = new QHBoxLayout(titleGroup);
        titleGroupLayout->setContentsMargins(0, 0, 0, 0);
        titleGroupLayout->setSpacing(8);
        titleGroupLayout->addWidget(title, 0, Qt::AlignBottom);
        titleGroupLayout->addWidget(versionLabel, 0, Qt::AlignBottom);
        titleGroupLayout->addWidget(updateLabel_, 0, Qt::AlignBottom);

        titleRow->addWidget(titleGroup, 0, Qt::AlignVCenter);
        titleRow->addStretch(1);
        titleRow->addWidget(settingsButton_, 0, Qt::AlignVCenter);

        auto *subtitle = new QLabel(header);
        subtitle->setObjectName("subtitleLabel");
        addRetranslation([subtitle]{ subtitle->setText(tr("Local, private redaction of faces and license plates in photos and videos")); });
        headerLayout->addLayout(titleRow);
        headerLayout->addWidget(subtitle);
        root->addWidget(header);

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(12);

            auto *modelTitle = makeSectionTitle(card);
            cardLayout->addWidget(modelTitle);
            addRetranslation([modelTitle]{ modelTitle->setText(tr("Model")); });

            auto *modelHint = makeSectionHint(card);
            cardLayout->addWidget(modelHint);
            addRetranslation([modelHint]
                             { modelHint->setText(tr("Choose speed vs. accuracy, or load a custom SCRFD ONNX file.")); });

            detectCombo_ = new QComboBox(card);
            detectCombo_->setMinimumHeight(34);
            detectCombo_->addItem(QString(), "faces");
            detectCombo_->addItem(QString(), "plates");
            detectCombo_->addItem(QString(), "both");
            addRetranslation([this]
                             {
                                 detectCombo_->setItemText(0, tr("Faces"));
                                 detectCombo_->setItemText(1, tr("License plates"));
                                 detectCombo_->setItemText(2, tr("Faces + license plates"));
                             });
            cardLayout->addWidget(detectCombo_);

            modelCombo_ = new QComboBox(card);
            modelCombo_->setMinimumHeight(34);
            cardLayout->addWidget(modelCombo_);
            connect(detectCombo_, &QComboBox::currentIndexChanged, this, [this]
            {
                const bool facesNeeded = detectCombo_->currentData().toString() != "plates";
                modelCombo_->setEnabled(facesNeeded);
                modelPathEdit_->setEnabled(facesNeeded);
            });

            auto *pathRow = new QHBoxLayout();
            pathRow->setSpacing(8);
            modelPathEdit_ = new QLineEdit(card);
            modelPathEdit_->setReadOnly(true);
            addRetranslation([this]{ modelPathEdit_->setPlaceholderText(tr("Bundled SCRFD model path")); });
            downloadButton_ = new QPushButton(card);
            downloadButton_->setObjectName("primaryButton");
            addRetranslation([this]{ downloadButton_->setText(tr("Download")); });
            downloadButton_->setCursor(Qt::PointingHandCursor);
            downloadButton_->setVisible(false);
            auto *modelButton = new QPushButton(card);
            addRetranslation([modelButton]{ modelButton->setText(tr("Browse…")); });
            modelButton->setCursor(Qt::PointingHandCursor);
            pathRow->addWidget(modelPathEdit_, 1);
            pathRow->addWidget(downloadButton_);
            pathRow->addWidget(modelButton);
            cardLayout->addLayout(pathRow);

            connect(downloadButton_, &QPushButton::clicked, this, &MainWindow::downloadSelectedModel);
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

            auto *inputsTitle = makeSectionTitle(card);
            cardLayout->addWidget(inputsTitle);
            addRetranslation([inputsTitle]{ inputsTitle->setText(tr("Inputs")); });

            auto *inputsHint = makeSectionHint(card);
            cardLayout->addWidget(inputsHint);
            addRetranslation([inputsHint]
                             { inputsHint->setText(tr("Drag images, videos, or folders here, or use the buttons below.")); });

            auto *dropList = new DropListWidget(card);
            inputList_ = dropList;
            inputList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
            inputList_->setMinimumHeight(140);
            inputList_->setAlternatingRowColors(false);
            inputList_->setIconSize(QSize(40, 40));
            addRetranslation([this, dropList]
                             {
                                 dropList->setPlaceholderText(tr("Drop images, videos, or folders here"));
                                 inputList_->setAccessibleName(tr("Input images and folders"));
                                 inputList_->setToolTip(tr("Right-click for options · Delete removes selected items"));
                             });
            cardLayout->addWidget(inputList_);

            auto *buttonRow = new QHBoxLayout();
            buttonRow->setSpacing(8);
            auto *addFiles = new QPushButton(card);
            addRetranslation([addFiles]{ addFiles->setText(tr("Add Files")); });
            auto *addFolder = new QPushButton(card);
            addRetranslation([addFolder]{ addFolder->setText(tr("Add Folder")); });
            auto *clearInputs = new QPushButton(card);
            addRetranslation([clearInputs]{ clearInputs->setText(tr("Clear")); });
            addFiles->setCursor(Qt::PointingHandCursor);
            addFolder->setCursor(Qt::PointingHandCursor);
            clearInputs->setCursor(Qt::PointingHandCursor);
            recursiveCheck_ = new QCheckBox(card);
            addRetranslation([this]{ recursiveCheck_->setText(tr("Include subfolders")); });
            recursiveCheck_->setChecked(true);
            reviewCheck_ = new QCheckBox(card);
            addRetranslation([this]{ reviewCheck_->setText(tr("Review each image")); });
            reviewCheck_->setChecked(false);
            addRetranslation([this]
                             {
                                 reviewCheck_->setToolTip(tr(
                                     "Before saving each image, open a preview where you can:\n"
                                     "  • Click a detected box to exclude it\n"
                                     "  • Drag an empty area to add a box the model missed\n"
                                     "Videos are processed without review."));
                             });

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

            auto *outputTitle = makeSectionTitle(card);
            cardLayout->addWidget(outputTitle);
            addRetranslation([outputTitle]{ outputTitle->setText(tr("Output")); });

            auto *outputHint = makeSectionHint(card);
            cardLayout->addWidget(outputHint);
            addRetranslation([outputHint]
                             { outputHint->setText(tr("Anonymized copies are written here, preserving folder structure.")); });

            auto *outputRow = new QHBoxLayout();
            outputRow->setSpacing(8);
            outputDirEdit_ = new QLineEdit(defaultOutputDirectory(), card);
            auto *outputButton = new QPushButton(card);
            addRetranslation([outputButton]{ outputButton->setText(tr("Choose…")); });
            outputButton->setCursor(Qt::PointingHandCursor);
            outputRow->addWidget(outputDirEdit_, 1);
            outputRow->addWidget(outputButton);
            cardLayout->addLayout(outputRow);
            connect(outputButton, &QPushButton::clicked, this, &MainWindow::chooseOutputDirectory);

            preserveMetaCheck_ = new QCheckBox(card);
            addRetranslation([this]
                             { preserveMetaCheck_->setText(tr("Preserve original metadata (EXIF, location, color profile)")); });
            preserveMetaCheck_->setChecked(false);
            addRetranslation([this]
                             {
                                 preserveMetaCheck_->setToolTip(tr(
                                     "Off (default): output carries no metadata — GPS, camera, and timestamps are removed.\n"
                                     "On: copies EXIF/IPTC/XMP and the ICC color profile from the original, and preserves "
                                     "format and bit depth at maximum quality. Best for archiving high-resolution photos."));
                             });
            cardLayout->addWidget(preserveMetaCheck_);

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
            addRetranslation([this]{ advancedToggle_->setText(tr("Advanced Options")); });
            advancedToggle_->setCheckable(true);
            advancedToggle_->setChecked(false);
            advancedToggle_->setArrowType(Qt::RightArrow);
            advancedToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            advancedToggle_->setCursor(Qt::PointingHandCursor);

            auto *resetButton = new QPushButton(card);
            addRetranslation([resetButton]{ resetButton->setText(tr("Reset to defaults")); });
            resetButton->setCursor(Qt::PointingHandCursor);

            headerRow->addWidget(advancedToggle_);
            headerRow->addStretch(1);
            headerRow->addWidget(resetButton);
            cardLayout->addLayout(headerRow);

            advancedBody_ = new QWidget(card);
            auto *bodyLayout = new QVBoxLayout(advancedBody_);
            bodyLayout->setContentsMargins(0, 14, 0, 4);
            bodyLayout->setSpacing(12);

            auto *advancedHint = makeSectionHint(advancedBody_);
            bodyLayout->addWidget(advancedHint);
            addRetranslation([advancedHint]
                             { advancedHint->setText(tr("Tweak detection and mosaic behavior. Defaults work for most photos.")); });

            methodCombo_ = new QComboBox(advancedBody_);
            methodCombo_->addItem(QString(), static_cast<int>(AnonymizationMethod::Mosaic));
            methodCombo_->addItem(QString(), static_cast<int>(AnonymizationMethod::Blur));
            methodCombo_->addItem(QString(), static_cast<int>(AnonymizationMethod::Fill));
            addRetranslation([this]
                             {
                                 methodCombo_->setItemText(0, tr("Mosaic (pixelate)"));
                                 methodCombo_->setItemText(1, tr("Gaussian blur"));
                                 methodCombo_->setItemText(2, tr("Solid fill (blackout)"));
                             });
            addRetranslation([this]
                             {
                                 methodCombo_->setToolTip(tr(
                                     "How detected faces are obscured.\n"
                                     "Mosaic = pixelation (block size below).\n"
                                     "Gaussian blur = strong smoothing scaled to face size.\n"
                                     "Solid fill = opaque black box, irreversible. Default: Mosaic"));
                             });

            shapeCombo_ = new QComboBox(advancedBody_);
            shapeCombo_->addItem(QString(), static_cast<int>(MaskShape::Rectangle));
            shapeCombo_->addItem(QString(), static_cast<int>(MaskShape::Ellipse));
            addRetranslation([this]
                             {
                                 shapeCombo_->setItemText(0, tr("Rectangle"));
                                 shapeCombo_->setItemText(1, tr("Rounded (ellipse)"));
                             });
            addRetranslation([this]
                             {
                                 shapeCombo_->setToolTip(tr(
                                     "Shape of the obscured region.\n"
                                     "Rectangle = full padded box.\n"
                                     "Rounded = elliptical mask that follows the face and leaves corners untouched. "
                                     "Default: Rectangle"));
                             });

            softEdgeCheck_ = new QCheckBox(advancedBody_);
            addRetranslation([this]
                             {
                                 softEdgeCheck_->setAccessibleName(tr("Soft edges"));
                                 softEdgeCheck_->setToolTip(tr(
                                     "Fades the edge of the obscured region into the photo "
                                     "instead of a hard cutoff.\n"
                                     "The fade only extends outward, so the detected area "
                                     "stays fully covered. Default: off"));
                             });

            scoreThresholdSpin_ = new QDoubleSpinBox(advancedBody_);
            scoreThresholdSpin_->setRange(0.05, 0.99);
            scoreThresholdSpin_->setSingleStep(0.05);
            scoreThresholdSpin_->setDecimals(2);
            scoreThresholdSpin_->setValue(kDefaultScoreThreshold);
            addRetranslation([this]
                             {
                                 scoreThresholdSpin_->setToolTip(tr(
                                     "Minimum confidence to accept a face.\n"
                                     "Higher = fewer false positives but may miss small or side-profile faces.\n"
                                     "Lower = catches more faces but may blur non-face regions. Default: 0.50"));
                             });

            nmsThresholdSpin_ = new QDoubleSpinBox(advancedBody_);
            nmsThresholdSpin_->setRange(0.05, 0.95);
            nmsThresholdSpin_->setSingleStep(0.05);
            nmsThresholdSpin_->setDecimals(2);
            nmsThresholdSpin_->setValue(kDefaultNmsThreshold);
            addRetranslation([this]
                             {
                                 nmsThresholdSpin_->setToolTip(tr(
                                     "Non-Maximum Suppression overlap threshold for duplicate boxes.\n"
                                     "Lower = more aggressively removes overlapping detections.\n"
                                     "Higher = allows more overlap. Default: 0.40"));
                             });

            blockSizeSpin_ = new QSpinBox(advancedBody_);
            blockSizeSpin_->setRange(2, 200);
            blockSizeSpin_->setValue(kDefaultBlockSize);
            addRetranslation([this]
                             {
                                 blockSizeSpin_->setToolTip(tr(
                                     "Mosaic block size in pixels.\n"
                                     "Larger = coarser blocks, harder to un-blur.\n"
                                     "Smaller = finer mosaic, higher recovery risk. Default: 14"));
                             });

            paddingSpin_ = new QDoubleSpinBox(advancedBody_);
            paddingSpin_->setRange(0.0, 1.0);
            paddingSpin_->setSingleStep(0.05);
            paddingSpin_->setDecimals(2);
            paddingSpin_->setValue(kDefaultPadding);
            addRetranslation([this]
                             {
                                 paddingSpin_->setToolTip(tr(
                                     "Extra margin around each detected face, as a fraction of its size.\n"
                                     "Covers ears, hairline, and chin that the detector may miss.\n"
                                     "0.00 = exact box, 0.18 = ~18% larger. Default: 0.18"));
                             });

            auto *grid = new QFormLayout();
            grid->setLabelAlignment(Qt::AlignLeft);
            grid->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
            grid->setHorizontalSpacing(18);
            grid->setVerticalSpacing(10);
            auto *methodLabel = makeFieldLabel(advancedBody_);
            addRetranslation([methodLabel]{ methodLabel->setText(tr("Anonymization")); });
            grid->addRow(methodLabel, methodCombo_);
            auto *shapeLabel = makeFieldLabel(advancedBody_);
            addRetranslation([shapeLabel]{ shapeLabel->setText(tr("Shape")); });
            grid->addRow(shapeLabel, shapeCombo_);
            auto *softEdgeLabel = makeFieldLabel(advancedBody_);
            addRetranslation([softEdgeLabel]{ softEdgeLabel->setText(tr("Soft edges")); });
            grid->addRow(softEdgeLabel, softEdgeCheck_);
            auto *scoreLabel = makeFieldLabel(advancedBody_);
            addRetranslation([scoreLabel]{ scoreLabel->setText(tr("Score threshold")); });
            grid->addRow(scoreLabel, scoreThresholdSpin_);
            auto *nmsLabel = makeFieldLabel(advancedBody_);
            addRetranslation([nmsLabel]{ nmsLabel->setText(tr("NMS threshold")); });
            grid->addRow(nmsLabel, nmsThresholdSpin_);
            auto *blockLabel = makeFieldLabel(advancedBody_);
            addRetranslation([blockLabel]{ blockLabel->setText(tr("Mosaic block size")); });
            grid->addRow(blockLabel, blockSizeSpin_);
            auto *paddingLabel = makeFieldLabel(advancedBody_);
            addRetranslation([paddingLabel]{ paddingLabel->setText(tr("Face padding")); });
            grid->addRow(paddingLabel, paddingSpin_);

            samplePreview_ = new QLabel(advancedBody_);
            samplePreview_->setFixedSize(152, 96);
            addRetranslation([this]
                             {
                                 samplePreview_->setAccessibleName(tr("Anonymization style preview"));
                                 samplePreview_->setToolTip(tr(
                                     "Sample of the current anonymization style and block size."));
                             });
            auto *sampleLabel = makeFieldLabel(advancedBody_);
            addRetranslation([sampleLabel]{ sampleLabel->setText(tr("Preview")); });
            grid->addRow(sampleLabel, samplePreview_);
            bodyLayout->addLayout(grid);

            advancedBody_->setVisible(false);
            cardLayout->addWidget(advancedBody_);

            connect(advancedToggle_, &QToolButton::toggled, this, &MainWindow::toggleAdvanced);
            connect(resetButton, &QPushButton::clicked, this, &MainWindow::resetAdvancedDefaults);
            connect(methodCombo_, &QComboBox::currentIndexChanged, this,
                    [this]{ updateAnonymizationSample(); });
            connect(shapeCombo_, &QComboBox::currentIndexChanged, this,
                    [this]{ updateAnonymizationSample(); });
            connect(softEdgeCheck_, &QCheckBox::toggled, this,
                    [this]{ updateAnonymizationSample(); });
            connect(blockSizeSpin_, &QSpinBox::valueChanged, this,
                    [this]{ updateAnonymizationSample(); });
            connect(paddingSpin_, &QDoubleSpinBox::valueChanged, this,
                    [this]{ updateAnonymizationSample(); });
            updateAnonymizationSample();

            root->addWidget(card);
        }

        {
            auto *card = makeCard(central);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(20, 18, 20, 18);
            cardLayout->setSpacing(10);

            auto *activityTitle = makeSectionTitle(card);
            cardLayout->addWidget(activityTitle);
            addRetranslation([activityTitle]{ activityTitle->setText(tr("Activity")); });
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

        statusLabel_ = new QLabel(bottomBar);
        statusLabel_->setObjectName("statusLabel");
        addRetranslation([this]{ statusLabel_->setText(tr("Ready")); });

        progressBar_ = new QProgressBar(bottomBar);
        progressBar_->setRange(0, 100);
        progressBar_->setValue(0);
        progressBar_->setTextVisible(false);
        progressBar_->setFixedHeight(6);
        addRetranslation([this]{ progressBar_->setAccessibleName(tr("Processing progress")); });

        openOutputButton_ = new QPushButton(bottomBar);
        addRetranslation([this]{ openOutputButton_->setText(tr("Open Output Folder")); });
        openOutputButton_->setCursor(Qt::PointingHandCursor);
        openOutputButton_->setVisible(false);
        connect(openOutputButton_, &QPushButton::clicked, this, [this]
        {
            const QString dir = outputDirEdit_->text();
            if (!dir.isEmpty() && QFileInfo::exists(dir))
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
            }
        });

        stopButton_ = new QPushButton(bottomBar);
        stopButton_->setObjectName("dangerButton");
        addRetranslation([this]{ stopButton_->setText(tr("Stop")); });
        stopButton_->setCursor(Qt::PointingHandCursor);
        stopButton_->setEnabled(false);

        startButton_ = new QPushButton(bottomBar);
        startButton_->setObjectName("primaryButton");
        addRetranslation([this]{ startButton_->setText(tr("Start")); });
        startButton_->setCursor(Qt::PointingHandCursor);
        startButton_->setDefault(true);

        connect(startButton_, &QPushButton::clicked, this, &MainWindow::startProcessing);
        connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopProcessing);

        bottomLayout->addWidget(statusLabel_);
        bottomLayout->addWidget(progressBar_, 1);
        bottomLayout->addWidget(openOutputButton_);
        bottomLayout->addWidget(stopButton_);
        bottomLayout->addWidget(startButton_);

        containerLayout->addWidget(bottomBar);

        setCentralWidget(container);
        statusBar()->hide();

        populateBundledModels();
        loadSettings();
        updateSettingsIcon();
        appendLog(tr("Ready. Drop images, videos, or folders to begin."));

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme)
        {
            if (themeMode_ == ThemeMode::System)
            {
                applyTheme(*qApp, ThemeMode::System);
                updateSettingsIcon();
            }
        });
#endif

        checkForUpdates();
    }

    MainWindow::~MainWindow()
    {
        QThread *thread = workerThread_;
        if (thread != nullptr)
        {
            shuttingDown_ = true;
            if (worker_ != nullptr)
            {
                worker_->cancel();
            }
            thread->quit();
            while (!thread->wait(50))
            {
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
        }
    }

    namespace
    {
        bool acceptableDropPath(const QFileInfo &info)
        {
            if (!info.exists())
            {
                return false;
            }
            if (info.isDir())
            {
                return true;
            }
            if (!info.isFile())
            {
                return false;
            }
            const auto path = pathFromQString(info.filePath());
            return isSupportedImage(path) || isSupportedVideo(path);
        }

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
                if (acceptableDropPath(QFileInfo(url.toLocalFile())))
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
            setDropHighlight(true);
            event->acceptProposedAction();
        } else
        {
            event->ignore();
        }
    }

    void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
    {
        setDropHighlight(false);
        QMainWindow::dragLeaveEvent(event);
    }

    void MainWindow::dropEvent(QDropEvent *event)
    {
        setDropHighlight(false);
        if (!hasAcceptableLocalUrls(event->mimeData()))
        {
            event->ignore();
            return;
        }
        int unsupportedCount = 0;
        for (const auto &url: event->mimeData()->urls())
        {
            if (!url.isLocalFile())
            {
                continue;
            }
            const QFileInfo info(url.toLocalFile());
            if (acceptableDropPath(info))
            {
                addInputPath(url.toLocalFile());
            }
            else if (info.exists() && info.isFile())
            {
                ++unsupportedCount;
            }
        }
        if (unsupportedCount > 0)
        {
            appendLog(tr("Ignored %n unsupported file(s).", nullptr, unsupportedCount));
        }
        event->acceptProposedAction();
    }

    void MainWindow::chooseModel()
    {
        const auto path = QFileDialog::getOpenFileName(this, tr("Select SCRFD ONNX Model"), QDir::currentPath(),
                                                       tr("ONNX Models (*.onnx)"));
        if (!path.isEmpty())
        {
            if (!customModelFileIsAllowed(this, path) || !confirmTrustedCustomModel(this, path))
            {
                return;
            }
            const QFileInfo info(path);
            modelCombo_->addItem(tr("Custom — %1").arg(info.fileName()), info.absoluteFilePath());
            modelCombo_->setCurrentIndex(modelCombo_->count() - 1);
            modelPathEdit_->setText(path);
        }
    }

    void MainWindow::chooseFiles()
    {
        const auto files = QFileDialog::getOpenFileNames(this,
                                                         tr("Select Images or Videos"),
                                                         QStandardPaths::writableLocation(
                                                             QStandardPaths::PicturesLocation),
                                                         tr("Images & Videos (*.jpg *.jpeg *.png *.bmp *.tif *.tiff *.webp *.mp4 *.mov *.m4v)"));
        for (const auto &file: files)
        {
            addInputPath(file);
        }
    }

    void MainWindow::chooseFolder()
    {
        const auto folder = QFileDialog::getExistingDirectory(this, tr("Select Folder"),
                                                              QStandardPaths::writableLocation(
                                                                  QStandardPaths::PicturesLocation));
        if (!folder.isEmpty())
        {
            addInputPath(folder);
        }
    }

    void MainWindow::chooseOutputDirectory()
    {
        const auto folder = QFileDialog::getExistingDirectory(this, tr("Select Output Folder"), outputDirEdit_->text());
        if (!folder.isEmpty())
        {
            outputDirEdit_->setText(folder);
        }
    }

    void MainWindow::startProcessing()
    {
        const QString detectTarget = detectCombo_ ? detectCombo_->currentData().toString()
                                                   : QStringLiteral("faces");
        const bool detectFaces = detectTarget != "plates";
        const bool detectPlates = detectTarget != "faces";

        const auto modelPath = selectedModelPath();
        const bool isCustom = !modelPath.isEmpty() && findBuiltinModel(modelPath) == nullptr;

        if (detectFaces)
        {
            if (modelPath.isEmpty())
            {
                reportValidationIssue(tr("Choose a SCRFD ONNX model first."), modelCombo_);
                return;
            }

            if (!QFileInfo::exists(modelPath))
            {
                const BuiltinModel *builtin = isCustom ? nullptr : findBuiltinModel(modelPath);
                if (builtin == nullptr)
                {
                    appendLog(tr("Choose a valid SCRFD ONNX model first."));
                    return;
                }
                appendLog(tr("Downloading %1…").arg(builtin->fileName));
                if (!ensureBuiltinModelAvailable(this, *builtin, modelPath))
                {
                    appendLog(tr("Model download was cancelled or failed."));
                    return;
                }
                updateModelPathFromSelection();
                appendLog(tr("Model ready: %1").arg(builtin->fileName));
            }

            if (isCustom && !customModelFileIsAllowed(this, modelPath))
            {
                return;
            }
        }

        QString plateModelPath;
        if (detectPlates)
        {
            plateModelPath = firstExistingModelPath(plateModel().fileName);
            if (plateModelPath.isEmpty())
            {
                plateModelPath = modelCacheDir() + "/" + plateModel().fileName;
                appendLog(tr("Downloading %1…").arg(plateModel().fileName));
                if (!ensurePlateModelAvailable(this, plateModelPath))
                {
                    appendLog(tr("Model download was cancelled or failed."));
                    return;
                }
                appendLog(tr("Model ready: %1").arg(plateModel().fileName));
            }
        }

        if (inputList_->count() == 0)
        {
            reportValidationIssue(tr("Add at least one image or folder."), inputList_);
            return;
        }

        if (outputDirEdit_->text().isEmpty())
        {
            reportValidationIssue(tr("Choose an output folder."), outputDirEdit_);
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
                reportValidationIssue(tr("Refusing to run: output folder is inside input '%1'. "
                                         "Pick a different output folder so originals aren't overwritten.")
                                          .arg(input),
                                      outputDirEdit_);
                return;
            }
        }

        setProcessing(true);
        openOutputButton_->setVisible(false);
        runTimer_.start();
        progressBar_->setValue(0);
        statusLabel_->setText(tr("Starting…"));

        auto detectorForRun = (cachedDetectorModelPath_ == modelPath) ? cachedDetector_ : nullptr;
        auto videoDetectorForRun = (cachedDetectorModelPath_ == modelPath) ? cachedVideoDetector_ : nullptr;
        auto plateForRun = (!plateModelPath.isEmpty() && cachedPlateModelPath_ == plateModelPath)
                               ? cachedPlateDetector_
                               : nullptr;

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
                                      static_cast<MaskShape>(shapeCombo_->currentData().toInt()),
                                      softEdgeCheck_->isChecked(),
                                      preserveMetaCheck_->isChecked(),
                                      reviewCheck_->isChecked(),
                                      this,
                                      std::move(detectorForRun),
                                      detectFaces,
                                      detectPlates,
                                      plateModelPath,
                                      std::move(plateForRun),
                                      gpuAcceleration_,
                                      crfForQuality(static_cast<VideoQuality>(videoQuality_)),
                                      std::move(videoDetectorForRun));

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
                    const QString elided = statusLabel_->fontMetrics().elidedText(
                        fileName, Qt::ElideMiddle, 320);
                    statusLabel_->setText(QString("%1/%2  ·  %3  ·  %4")
                        .arg(index).arg(total).arg(stage, elided));
                });

        connect(worker_, &ProcessorWorker::finished, this, &MainWindow::onWorkerFinished);
        connect(workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
        connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);

        appendLog(tr("Starting…"));
        workerThread_->start();
    }

    void MainWindow::stopProcessing() const
    {
        if (worker_ != nullptr)
        {
            appendLog(tr("Stopping after the current processing step…"));
            statusLabel_->setText(tr("Stopping…"));
            worker_->cancel();
        }
    }

    void MainWindow::onWorkerFinished(RunOutcome outcome)
    {
        setProcessing(false);
        const qint64 seconds = runTimer_.isValid() ? runTimer_.elapsed() / 1000 : 0;
        const QString elapsed = seconds >= 60
                                    ? QStringLiteral("%1m %2s").arg(seconds / 60).arg(seconds % 60)
                                    : QStringLiteral("%1s").arg(seconds);
        switch (outcome)
        {
            case RunOutcome::Completed:
                appendLog(tr("Finished."));
                statusLabel_->setText(tr("Done") + QStringLiteral("  ·  ") + elapsed);
                openOutputButton_->setVisible(true);
                break;
            case RunOutcome::Cancelled:
                appendLog(tr("Cancelled."));
                statusLabel_->setText(tr("Cancelled") + QStringLiteral("  ·  ") + elapsed);
                openOutputButton_->setVisible(true);
                break;
            case RunOutcome::Failed:
                appendLog(tr("Failed — check the log for details."));
                statusLabel_->setProperty("state", "warning");
                statusLabel_->style()->unpolish(statusLabel_);
                statusLabel_->style()->polish(statusLabel_);
                statusLabel_->setText(QStringLiteral("⚠  ") + tr("Failed — check the log"));
                break;
        }

        if (worker_ != nullptr)
        {
            auto detector = worker_->takeDetector();
            if (detector)
            {
                cachedDetector_ = std::move(detector);
                cachedDetectorModelPath_ = selectedModelPath();
            }

            auto videoDetector = worker_->takeVideoDetector();
            if (videoDetector)
            {
                cachedVideoDetector_ = std::move(videoDetector);
            }

            auto plate = worker_->takePlateDetector();
            if (plate)
            {
                cachedPlateDetector_ = std::move(plate);
                cachedPlateModelPath_ = firstExistingModelPath(plateModel().fileName);
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
        shapeCombo_->setCurrentIndex(0);
        softEdgeCheck_->setChecked(false);
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
            modelCombo_->addItem(tr("Custom — %1").arg(info.fileName()), info.absoluteFilePath());
            modelCombo_->setCurrentIndex(modelCombo_->count() - 1);
        }

        const auto outputDir = settings.value("outputDir").toString();
        if (!outputDir.isEmpty())
        {
            outputDirEdit_->setText(outputDir);
        }

        recursiveCheck_->setChecked(settings.value("recursive", true).toBool());
        reviewCheck_->setChecked(settings.value("review", false).toBool());
        preserveMetaCheck_->setChecked(settings.value("preserveMetadata", false).toBool());

        scoreThresholdSpin_->setValue(settings.value("scoreThreshold", kDefaultScoreThreshold).toDouble());
        nmsThresholdSpin_->setValue(settings.value("nmsThreshold", kDefaultNmsThreshold).toDouble());
        blockSizeSpin_->setValue(settings.value("blockSize", kDefaultBlockSize).toInt());
        paddingSpin_->setValue(settings.value("padding", kDefaultPadding).toDouble());

        const auto savedMethod = settings.value("method", 0).toInt();
        if (methodCombo_ != nullptr && savedMethod >= 0 && savedMethod < methodCombo_->count())
        {
            methodCombo_->setCurrentIndex(savedMethod);
        }

        const auto savedShape = settings.value("shape", 0).toInt();
        if (shapeCombo_ != nullptr && savedShape >= 0 && savedShape < shapeCombo_->count())
        {
            shapeCombo_->setCurrentIndex(savedShape);
        }

        if (softEdgeCheck_ != nullptr)
        {
            softEdgeCheck_->setChecked(settings.value("softEdges", false).toBool());
        }

        const auto savedDetect = settings.value("detectTarget", 0).toInt();
        if (detectCombo_ != nullptr && savedDetect >= 0 && savedDetect < detectCombo_->count())
        {
            detectCombo_->setCurrentIndex(savedDetect);
        }

        if (settings.value("advancedExpanded", false).toBool())
        {
            advancedToggle_->setChecked(true);
        }
        settings.endGroup();

        themeMode_ = themeModeFromString(settings.value("theme", "system").toString());
        checkForUpdatesOnStartup_ = settings.value("checkForUpdates", true).toBool();
        fileLogging_ = settings.value("fileLogging", true).toBool();
        gpuAcceleration_ = settings.value("gpuAcceleration", true).toBool();
        videoQuality_ = std::clamp(settings.value("videoQuality", 0).toInt(), 0, 2);

        const auto savedLanguage = settings.value("language").toString();
        QString language = savedLanguage;
        if (language.isEmpty())
        {
            language = (QLocale::system().language() == QLocale::Korean) ? QStringLiteral("ko") : QStringLiteral("en");
        }
        applyLanguage(language);

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

        const auto currentModel = selectedModelPath();
        if (!currentModel.isEmpty() && findBuiltinModel(currentModel) == nullptr)
        {
            settings.setValue("customModelPath", currentModel);
        } else
        {
            settings.remove("customModelPath");
        }

        settings.setValue("outputDir", outputDirEdit_->text());
        settings.setValue("recursive", recursiveCheck_->isChecked());
        settings.setValue("review", reviewCheck_->isChecked());
        settings.setValue("preserveMetadata", preserveMetaCheck_->isChecked());
        settings.setValue("scoreThreshold", scoreThresholdSpin_->value());
        settings.setValue("nmsThreshold", nmsThresholdSpin_->value());
        settings.setValue("blockSize", blockSizeSpin_->value());
        settings.setValue("padding", paddingSpin_->value());
        settings.setValue("method", methodCombo_ ? methodCombo_->currentIndex() : 0);
        settings.setValue("shape", shapeCombo_ ? shapeCombo_->currentIndex() : 0);
        settings.setValue("softEdges", softEdgeCheck_ != nullptr && softEdgeCheck_->isChecked());
        settings.setValue("detectTarget", detectCombo_ ? detectCombo_->currentIndex() : 0);
        settings.setValue("advancedExpanded", advancedToggle_ ? advancedToggle_->isChecked() : false);
        settings.endGroup();

        settings.setValue("theme", themeModeToString(themeMode_));
        settings.setValue("checkForUpdates", checkForUpdatesOnStartup_);
        settings.setValue("fileLogging", fileLogging_);
        settings.setValue("gpuAcceleration", gpuAcceleration_);
        settings.setValue("videoQuality", videoQuality_);

        settings.setValue("language", language_);
    }

    void MainWindow::checkForUpdates()
    {
        if (!checkForUpdatesOnStartup_)
        {
            return;
        }

        auto *checker = new UpdateChecker(QCoreApplication::applicationVersion(), this);
        connect(checker, &UpdateChecker::updateAvailable, this,
                [this](const QString &latestVersion, const QString &releaseUrl)
                {
                    if (updateLabel_ == nullptr)
                    {
                        return;
                    }
                    const QString text = tr("Update available: %1").arg(latestVersion);
                    updateLabel_->setText(QStringLiteral("<a href=\"%1\">%2</a>")
                        .arg(releaseUrl.toHtmlEscaped(), text.toHtmlEscaped()));
                    updateLabel_->setVisible(true);
                });
        checker->check();
    }

    void MainWindow::openSettings()
    {
        SettingsDialog dialog(themeMode_, language_, checkForUpdatesOnStartup_, fileLogging_,
                              gpuAcceleration_, videoQuality_, this);

        connect(&dialog, &SettingsDialog::themeChanged, this, [this](ThemeMode mode)
        {
            themeMode_ = mode;
            applyTheme(*qApp, mode);
            updateSettingsIcon();
            saveSettings();
        });
        connect(&dialog, &SettingsDialog::languageChanged, this, [this](const QString &language)
        {
            if (language != language_)
            {
                applyLanguage(language);
                saveSettings();
            }
        });
        connect(&dialog, &SettingsDialog::checkForUpdatesChanged, this, [this](bool enabled)
        {
            checkForUpdatesOnStartup_ = enabled;
            saveSettings();
        });
        connect(&dialog, &SettingsDialog::fileLoggingChanged, this, [this](bool enabled)
        {
            fileLogging_ = enabled;
            saveSettings();
        });
        connect(&dialog, &SettingsDialog::videoQualityChanged, this, [this](int quality)
        {
            videoQuality_ = std::clamp(quality, 0, 2);
            saveSettings();
        });
        connect(&dialog, &SettingsDialog::gpuAccelerationChanged, this, [this](bool enabled)
        {
            if (gpuAcceleration_ != enabled)
            {
                gpuAcceleration_ = enabled;
                cachedDetector_.reset();
                cachedVideoDetector_.reset();
                cachedDetectorModelPath_.clear();
                cachedPlateDetector_.reset();
                cachedPlateModelPath_.clear();
                saveSettings();
            }
        });

        dialog.exec();
    }

    void MainWindow::updateAnonymizationSample() const
    {
        if (samplePreview_ == nullptr)
        {
            return;
        }
        samplePreview_->setPixmap(anonymizationSamplePixmap(
            static_cast<AnonymizationMethod>(methodCombo_->currentData().toInt()),
            static_cast<MaskShape>(shapeCombo_->currentData().toInt()),
            blockSizeSpin_->value(),
            static_cast<float>(paddingSpin_->value()),
            softEdgeCheck_->isChecked()));
    }

    void MainWindow::updateSettingsIcon() const
    {
        if (settingsButton_ == nullptr)
        {
            return;
        }
        const QIcon icon = settingsGearIcon(themeMode_);
        if (icon.isNull())
        {
            settingsButton_->setText(QStringLiteral("⚙"));
        }
        else
        {
            settingsButton_->setText(QString());
            settingsButton_->setIcon(icon);
            settingsButton_->setIconSize(QSize(20, 20));
        }
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

        auto *item = new QListWidgetItem(path);
        if (info.isDir())
        {
            item->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
        }
        else if (isSupportedVideo(pathFromQString(path)))
        {
            const QIcon thumbnail = videoThumbnailIcon(path);
            item->setIcon(thumbnail.isNull()
                              ? style()->standardIcon(QStyle::SP_FileIcon)
                              : thumbnail);
        }
        else
        {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            QSize thumbSize = reader.size();
            if (thumbSize.isValid())
            {
                thumbSize.scale(40, 40, Qt::KeepAspectRatio);
                reader.setScaledSize(thumbSize);
            }
            const QImage thumb = reader.read();
            item->setIcon(thumb.isNull()
                              ? style()->standardIcon(QStyle::SP_FileIcon)
                              : QIcon(QPixmap::fromImage(thumb)));
        }
        inputList_->addItem(item);
    }

    void MainWindow::setProcessing(bool processing) const
    {
        if (statusLabel_->property("state").isValid())
        {
            statusLabel_->setProperty("state", QVariant());
            statusLabel_->style()->unpolish(statusLabel_);
            statusLabel_->style()->polish(statusLabel_);
        }

        startButton_->setEnabled(!processing);
        stopButton_->setEnabled(processing);
        settingsButton_->setEnabled(!processing);
        detectCombo_->setEnabled(!processing);
        modelCombo_->setEnabled(!processing);
        methodCombo_->setEnabled(!processing);
        shapeCombo_->setEnabled(!processing);
        softEdgeCheck_->setEnabled(!processing);
        modelPathEdit_->setEnabled(!processing);
        downloadButton_->setEnabled(!processing);
        outputDirEdit_->setEnabled(!processing);
        inputList_->setEnabled(!processing);
        recursiveCheck_->setEnabled(!processing);
        reviewCheck_->setEnabled(!processing);
        preserveMetaCheck_->setEnabled(!processing);
        scoreThresholdSpin_->setEnabled(!processing);
        nmsThresholdSpin_->setEnabled(!processing);
        blockSizeSpin_->setEnabled(!processing);
        paddingSpin_->setEnabled(!processing);

        if (!processing)
        {
            const bool facesNeeded = detectCombo_->currentData().toString() != "plates";
            modelCombo_->setEnabled(facesNeeded);
            modelPathEdit_->setEnabled(facesNeeded);
        }
    }

    void MainWindow::setDropHighlight(bool active) const
    {
        inputList_->setProperty("dragActive", active ? QVariant(true) : QVariant());
        inputList_->style()->unpolish(inputList_);
        inputList_->style()->polish(inputList_);
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

    void MainWindow::populateBundledModels()
    {
        for (const auto &model: builtinModels())
        {
            const auto existing = firstExistingModelPath(model.fileName);
            const auto path = existing.isEmpty() ? modelCacheDir() + "/" + model.fileName : existing;
            modelCombo_->addItem(QString(), path);
        }

        addRetranslation([this]
                         {
                             if (modelCombo_->count() >= 2)
                             {
                                 modelCombo_->setItemText(0, tr("Fast  ·  SCRFD 2.5G"));
                                 modelCombo_->setItemText(1, tr("Accurate  ·  SCRFD 10G"));
                             }
                         });

        modelCombo_->setCurrentIndex(0);
        updateModelPathFromSelection();
    }

    void MainWindow::downloadSelectedModel()
    {
        const auto modelPath = selectedModelPath();
        const BuiltinModel *builtin = findBuiltinModel(modelPath);
        if (builtin == nullptr)
        {
            return;
        }
        if (QFileInfo::exists(modelPath))
        {
            updateModelPathFromSelection();
            return;
        }

        appendLog(tr("Downloading %1…").arg(builtin->fileName));
        if (ensureBuiltinModelAvailable(this, *builtin, modelPath))
        {
            appendLog(tr("Model ready: %1").arg(builtin->fileName));
        }
        else
        {
            appendLog(tr("Model download was cancelled or failed."));
        }
        updateModelPathFromSelection();
    }

    void MainWindow::updateModelPathFromSelection() const
    {
        const auto path = selectedModelPath();
        const bool exists = !path.isEmpty() && QFileInfo::exists(path);
        const bool missingBuiltin = !exists && findBuiltinModel(path) != nullptr;

        if (path.isEmpty() || exists)
        {
            modelPathEdit_->setText(path);
        }
        else if (missingBuiltin)
        {
            modelPathEdit_->setText(tr("Not downloaded yet — click Download"));
        }
        else
        {
            modelPathEdit_->setText(path);
        }

        if (downloadButton_ != nullptr)
        {
            downloadButton_->setVisible(missingBuiltin);
        }
    }

    QString MainWindow::selectedModelPath() const
    {
        if (modelCombo_ == nullptr || modelCombo_->currentIndex() < 0)
        {
            return {};
        }

        return modelCombo_->currentData().toString();
    }

    void MainWindow::addRetranslation(std::function<void()> apply)
    {
        apply();
        retranslators_.push_back(std::move(apply));
    }

    void MainWindow::retranslateUi()
    {
        for (const auto &apply: retranslators_)
        {
            apply();
        }
    }

    void MainWindow::applyLanguage(const QString &language)
    {
        qApp->removeTranslator(&translator_);
        qApp->removeTranslator(&qtTranslator_);
        if (language != "en")
        {
            if (translator_.load(":/i18n/redactly_" + language + ".qm"))
            {
                qApp->installTranslator(&translator_);
            }
            if (qtTranslator_.load("qtbase_" + language,
                                   QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            {
                qApp->installTranslator(&qtTranslator_);
            }
        }
        language_ = language;
    }

    void MainWindow::changeEvent(QEvent *event)
    {
        if (event->type() == QEvent::LanguageChange)
        {
            retranslateUi();
        }
        QMainWindow::changeEvent(event);
    }

    void MainWindow::appendLog(const QString &message) const
    {
        const auto time = QDateTime::currentDateTime().toString("HH:mm:ss");
        logEdit_->appendPlainText(QString("[%1]  %2").arg(time, message));
        spdlog::info("{}", message.toStdString());
    }

    void MainWindow::reportValidationIssue(const QString &message, QWidget *field) const
    {
        statusLabel_->setProperty("state", "warning");
        statusLabel_->style()->unpolish(statusLabel_);
        statusLabel_->style()->polish(statusLabel_);
        statusLabel_->setText(QStringLiteral("⚠  ") + message);
        appendLog(message);

        if (field != nullptr)
        {
            field->setProperty("invalid", true);
            field->style()->unpolish(field);
            field->style()->polish(field);
            QTimer::singleShot(2200, field, [field]
            {
                field->setProperty("invalid", QVariant());
                field->style()->unpolish(field);
                field->style()->polish(field);
            });
            field->setFocus(Qt::OtherFocusReason);
        }
    }

    ReviewResult MainWindow::requestReview(const QImage &image,
                                           const QString &sourceName,
                                           const QVector<QRectF> &detected,
                                           int currentIndex,
                                           int total,
                                           double previewScale)
    {
        if (shuttingDown_)
        {
            ReviewResult cancelAll;
            cancelAll.decision = ReviewDecision::CancelAll;
            return cancelAll;
        }
        ReviewPreviewSpec spec;
        spec.method = static_cast<AnonymizationMethod>(methodCombo_->currentData().toInt());
        spec.shape = static_cast<MaskShape>(shapeCombo_->currentData().toInt());
        spec.softEdges = softEdgeCheck_ != nullptr && softEdgeCheck_->isChecked();
        spec.blockSize = blockSizeSpin_->value();
        spec.padding = static_cast<float>(paddingSpin_->value());
        spec.previewScale = previewScale;
        ReviewDialog dialog(image, sourceName, detected, currentIndex, total,
                            preserveMetaCheck_ != nullptr && preserveMetaCheck_->isChecked(),
                            spec, this);
        dialog.exec();
        return dialog.result();
    }
}
