#include "faceveil/ReviewDialog.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>

namespace faceveil
{
    namespace
    {
        struct Box
        {
            QRectF rect;
            bool detected;
            bool included;
        };

        constexpr int kMinBoxPixels = 6;
    }

    class ReviewCanvas final : public QWidget
    {
    public:
        ReviewCanvas(QImage image, const QVector<QRectF> &detected, QWidget *parent)
            : QWidget(parent), image_(std::move(image))
        {
            setMouseTracking(true);
            setFocusPolicy(Qt::StrongFocus);
            const QRectF imageBounds(QPointF(0, 0), QSizeF(image_.size()));
            boxes_.reserve(detected.size());
            for (const auto &rect: detected)
            {
                const QRectF clamped = rect.intersected(imageBounds);
                if (clamped.width() >= 1.0 && clamped.height() >= 1.0)
                {
                    boxes_.push_back(Box{clamped, true, true});
                }
            }
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }

        [[nodiscard]] QVector<QRectF> finalBoxes() const
        {
            QVector<QRectF> result;
            result.reserve(boxes_.size());
            const QRectF imageBounds(QPointF(0, 0), QSizeF(image_.size()));
            for (const auto &box: boxes_)
            {
                if (!box.detected || box.included)
                {

                    const QRectF clamped = box.rect.intersected(imageBounds);
                    if (clamped.width() >= 1.0 && clamped.height() >= 1.0)
                    {
                        result.push_back(clamped);
                    }
                }
            }
            return result;
        }

        [[nodiscard]] bool canUndo() const { return !undoStack_.empty(); }
        [[nodiscard]] bool canRedo() const { return !redoStack_.empty(); }

        void undo()
        {
            if (undoStack_.empty())
            {
                return;
            }
            redoStack_.push_back(boxes_);
            boxes_ = undoStack_.back();
            undoStack_.pop_back();
            hoveredIndex_ = -1;
            update();
            emitHistoryChanged();
        }

        void redo()
        {
            if (redoStack_.empty())
            {
                return;
            }
            undoStack_.push_back(boxes_);
            boxes_ = redoStack_.back();
            redoStack_.pop_back();
            hoveredIndex_ = -1;
            update();
            emitHistoryChanged();
        }

        void setHistoryChangedCallback(std::function<void()> cb)
        {
            historyChanged_ = std::move(cb);
        }

        [[nodiscard]] QSize sizeHint() const override
        {
            return fitSize(image_.size());
        }

        [[nodiscard]] QSize minimumSizeHint() const override
        {
            return QSize(420, 320);
        }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.fillRect(rect(), QColor("#111827"));

            if (image_.isNull())
            {
                return;
            }

            const QRectF target = imageTargetRect();
            painter.drawImage(target, image_);

            for (int i = 0; i < boxes_.size(); ++i)
            {
                const auto &box = boxes_[i];
                const QRectF screen = imageToScreen(box.rect);

                QColor stroke;
                QColor fill;
                if (!box.detected)
                {
                    stroke = QColor("#3B82F6");
                    fill = QColor(59, 130, 246, 60);
                }
                else if (box.included)
                {
                    stroke = QColor("#F59E0B");
                    fill = QColor(245, 158, 11, 60);
                }
                else
                {
                    stroke = QColor("#9CA3AF");
                    fill = QColor(156, 163, 175, 30);
                }

                painter.setPen(QPen(stroke, 2));
                painter.setBrush(fill);
                painter.drawRect(screen);

                if (box.detected && !box.included)
                {
                    painter.setPen(QPen(QColor("#EF4444"), 2));
                    painter.drawLine(screen.topLeft(), screen.bottomRight());
                    painter.drawLine(screen.topRight(), screen.bottomLeft());
                }

                if (i == hoveredIndex_)
                {
                    painter.setPen(QPen(QColor("#FFFFFF"), 1, Qt::DashLine));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRect(screen.adjusted(-2, -2, 2, 2));
                }
            }

            if (drawing_)
            {
                painter.setPen(QPen(QColor("#3B82F6"), 2, Qt::DashLine));
                painter.setBrush(QColor(59, 130, 246, 40));
                painter.drawRect(QRectF(dragStart_, dragCurrent_).normalized());
            }
        }

        void mousePressEvent(QMouseEvent *event) override
        {
            if (event->button() != Qt::LeftButton || image_.isNull())
            {
                return;
            }
            const QPointF pos = event->position();
            const int hit = hitTest(pos);
            if (hit >= 0)
            {
                pushUndoSnapshot();
                auto &box = boxes_[hit];
                if (box.detected)
                {
                    box.included = !box.included;
                }
                else
                {
                    boxes_.remove(hit);
                }
                update();
                return;
            }
            if (!imageTargetRect().contains(pos))
            {
                return;
            }
            drawing_ = true;
            dragStart_ = pos;
            dragCurrent_ = pos;
            update();
        }

        void mouseMoveEvent(QMouseEvent *event) override
        {
            const QPointF pos = event->position();
            if (drawing_)
            {
                dragCurrent_ = pos;
                update();
                return;
            }
            const int hit = hitTest(pos);
            if (hit != hoveredIndex_)
            {
                hoveredIndex_ = hit;
                setCursor(hit >= 0 ? Qt::PointingHandCursor : Qt::CrossCursor);
                update();
            }
        }

        void mouseReleaseEvent(QMouseEvent *event) override
        {
            if (!drawing_ || event->button() != Qt::LeftButton)
            {
                return;
            }
            drawing_ = false;
            const QRectF screenRect = QRectF(dragStart_, dragCurrent_).normalized();
            if (screenRect.width() >= kMinBoxPixels && screenRect.height() >= kMinBoxPixels)
            {
                const QRectF imageRect = screenToImage(screenRect).intersected(
                    QRectF(QPointF(0, 0), QSizeF(image_.size())));
                if (imageRect.width() >= 1.0 && imageRect.height() >= 1.0)
                {
                    pushUndoSnapshot();
                    boxes_.push_back(Box{imageRect, false, true});
                }
            }
            update();
        }

    private:
        [[nodiscard]] QRectF imageTargetRect() const
        {
            if (image_.isNull())
            {
                return {};
            }
            const QSize fitted = fitSize(image_.size());
            const double x = (width() - fitted.width()) / 2.0;
            const double y = (height() - fitted.height()) / 2.0;
            return QRectF(QPointF(x, y), QSizeF(fitted));
        }

        [[nodiscard]] QSize fitSize(QSize source) const
        {
            if (source.isEmpty())
            {
                return QSize(420, 320);
            }
            const double aspect = static_cast<double>(source.width()) / source.height();
            const int w = std::max(1, width());
            const int h = std::max(1, height());
            double fitW = w;
            double fitH = fitW / aspect;
            if (fitH > h)
            {
                fitH = h;
                fitW = fitH * aspect;
            }
            return QSize(static_cast<int>(fitW), static_cast<int>(fitH));
        }

        [[nodiscard]] QRectF imageToScreen(const QRectF &rect) const
        {
            const QRectF target = imageTargetRect();
            const double sx = target.width() / image_.width();
            const double sy = target.height() / image_.height();
            return QRectF(target.x() + rect.x() * sx,
                          target.y() + rect.y() * sy,
                          rect.width() * sx,
                          rect.height() * sy);
        }

        [[nodiscard]] QRectF screenToImage(const QRectF &rect) const
        {
            const QRectF target = imageTargetRect();
            if (target.width() <= 0 || target.height() <= 0)
            {
                return {};
            }
            const double sx = image_.width() / target.width();
            const double sy = image_.height() / target.height();
            return QRectF((rect.x() - target.x()) * sx,
                          (rect.y() - target.y()) * sy,
                          rect.width() * sx,
                          rect.height() * sy);
        }

        [[nodiscard]] int hitTest(QPointF pos) const
        {
            for (int i = boxes_.size() - 1; i >= 0; --i)
            {
                if (imageToScreen(boxes_[i].rect).contains(pos))
                {
                    return i;
                }
            }
            return -1;
        }

        void pushUndoSnapshot()
        {
            undoStack_.push_back(boxes_);
            redoStack_.clear();
            if (undoStack_.size() > kMaxUndo)
            {
                undoStack_.erase(undoStack_.begin());
            }
            emitHistoryChanged();
        }

        void emitHistoryChanged() const
        {
            if (historyChanged_)
            {
                historyChanged_();
            }
        }

        static constexpr std::size_t kMaxUndo = 64;

        QImage image_;
        QVector<Box> boxes_;
        int hoveredIndex_ = -1;
        bool drawing_ = false;
        QPointF dragStart_;
        QPointF dragCurrent_;
        std::vector<QVector<Box>> undoStack_;
        std::vector<QVector<Box>> redoStack_;
        std::function<void()> historyChanged_;
    };

    ReviewDialog::ReviewDialog(const QImage &image,
                               const QString &sourceName,
                               const QVector<QRectF> &detected,
                               int currentIndex,
                               int total,
                               QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QString("Review — %1").arg(sourceName));
        setModal(true);
        resize(960, 720);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(18, 18, 18, 18);
        root->setSpacing(12);

        auto *header = new QLabel(
            QString("<b>%1</b> &nbsp;·&nbsp; <span style='color:#6B7280'>%2 / %3</span>")
                .arg(sourceName.toHtmlEscaped()).arg(currentIndex).arg(total), this);
        header->setTextFormat(Qt::RichText);
        root->addWidget(header);

        canvas_ = new ReviewCanvas(image, detected, this);
        canvas_->setStyleSheet("background-color: #111827; border-radius: 8px;");
        root->addWidget(canvas_, 1);

        hintLabel_ = new QLabel(
            QString("Click a box to toggle · Drag an empty area to add · "
                    "Click a blue box to delete · %1 / %2 to undo/redo")
                .arg(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText),
                     QKeySequence(QKeySequence::Redo).toString(QKeySequence::NativeText)), this);
        hintLabel_->setStyleSheet("color: #6B7280; font-size: 12px;");
        root->addWidget(hintLabel_);

        auto *buttonRow = new QHBoxLayout();
        buttonRow->setSpacing(8);

        auto *cancelAll = new QPushButton("Cancel All", this);
        cancelAll->setCursor(Qt::PointingHandCursor);

        auto *undoButton = new QPushButton("Undo", this);
        undoButton->setCursor(Qt::PointingHandCursor);
        undoButton->setEnabled(false);

        auto *redoButton = new QPushButton("Redo", this);
        redoButton->setCursor(Qt::PointingHandCursor);
        redoButton->setEnabled(false);

        auto *doNotSave = new QPushButton("Do Not Save", this);
        doNotSave->setCursor(Qt::PointingHandCursor);

        auto *copyOriginal = new QPushButton("Copy Original", this);
        copyOriginal->setCursor(Qt::PointingHandCursor);

        auto *save = new QPushButton("Save && Next", this);
        save->setObjectName("primaryButton");
        save->setCursor(Qt::PointingHandCursor);
        save->setDefault(true);

        buttonRow->addWidget(cancelAll);
        buttonRow->addWidget(undoButton);
        buttonRow->addWidget(redoButton);
        buttonRow->addStretch(1);
        buttonRow->addWidget(doNotSave);
        buttonRow->addWidget(copyOriginal);
        buttonRow->addWidget(save);
        root->addLayout(buttonRow);

        connect(cancelAll, &QPushButton::clicked, this, [this]
        {
            decision_ = ReviewDecision::CancelAll;
            reject();
        });
        connect(undoButton, &QPushButton::clicked, this, [this] { canvas_->undo(); });
        connect(redoButton, &QPushButton::clicked, this, [this] { canvas_->redo(); });
        connect(doNotSave, &QPushButton::clicked, this, [this]
        {
            decision_ = ReviewDecision::DoNotSave;
            accept();
        });
        connect(copyOriginal, &QPushButton::clicked, this, [this]
        {
            decision_ = ReviewDecision::CopyOriginal;
            accept();
        });
        connect(save, &QPushButton::clicked, this, [this]
        {
            decision_ = ReviewDecision::Save;
            accept();
        });

        canvas_->setHistoryChangedCallback([this, undoButton, redoButton]
        {
            undoButton->setEnabled(canvas_->canUndo());
            redoButton->setEnabled(canvas_->canRedo());
        });

        auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
        auto *redoShortcut = new QShortcut(QKeySequence::Redo, this);
        connect(undoShortcut, &QShortcut::activated, this, [this] { canvas_->undo(); });
        connect(redoShortcut, &QShortcut::activated, this, [this] { canvas_->redo(); });
    }

    ReviewResult ReviewDialog::result() const
    {
        ReviewResult res;
        res.decision = decision_;
        res.finalBoxes = canvas_->finalBoxes();
        return res;
    }

    void ReviewDialog::reject()
    {
        decision_ = ReviewDecision::CancelAll;
        QDialog::reject();
    }
}
