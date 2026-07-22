#include "cloakframe/VideoReviewDialog.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace cloakframe
{
    namespace
    {
        const VideoReviewBox *boxAtFrame(const VideoReviewTrack &track, int frame)
        {
            const auto it = std::lower_bound(
                track.boxes.cbegin(), track.boxes.cend(), frame,
                [](const VideoReviewBox &box, int value) { return box.frame < value; });
            return it != track.boxes.cend() && it->frame == frame ? &*it : nullptr;
        }

        QString formatTime(double seconds)
        {
            seconds = std::max(0.0, seconds);
            const int total = static_cast<int>(std::floor(seconds));
            const int hours = total / 3600;
            const int minutes = (total / 60) % 60;
            const int secs = total % 60;
            return hours > 0
                       ? QStringLiteral("%1:%2:%3")
                             .arg(hours)
                             .arg(minutes, 2, 10, QLatin1Char('0'))
                             .arg(secs, 2, 10, QLatin1Char('0'))
                       : QStringLiteral("%1:%2")
                             .arg(minutes)
                             .arg(secs, 2, 10, QLatin1Char('0'));
        }

        QImage extractPreviewFrame(const VideoReviewRequest &request, int frame)
        {
            if (request.ffmpegPath.isEmpty() || request.sourcePath.isEmpty()
                || request.fps <= 0.0 || !request.frameSize.isValid())
            {
                return {};
            }

            int previewWidth = request.frameSize.width();
            int previewHeight = request.frameSize.height();
            const int longEdge = std::max(previewWidth, previewHeight);
            if (longEdge > 960)
            {
                const double scale = 960.0 / static_cast<double>(longEdge);
                previewWidth = std::max(2, static_cast<int>(std::lround(previewWidth * scale)));
                previewHeight = std::max(2, static_cast<int>(std::lround(previewHeight * scale)));
            }
            previewWidth += previewWidth % 2;
            previewHeight += previewHeight % 2;

            QProcess process;
            const double seconds = static_cast<double>(frame) / request.fps;
            process.start(request.ffmpegPath,
                          {"-v", "error", "-nostdin",
                           "-ss", QString::number(seconds, 'f', 6),
                           "-i", request.sourcePath,
                           "-map", "0:v:0", "-frames:v", "1",
                           "-vf", QString("scale=%1:%2:flags=area")
                                      .arg(previewWidth).arg(previewHeight),
                           "-f", "image2pipe", "-c:v", "png", "-"});
            if (!process.waitForStarted(3000) || !process.waitForFinished(12000)
                || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            {
                process.kill();
                return {};
            }
            QImage image;
            image.loadFromData(process.readAllStandardOutput(), "PNG");
            return image;
        }
    }

    class VideoReviewCanvas final : public QWidget
    {
    public:
        explicit VideoReviewCanvas(QWidget *parent = nullptr) : QWidget(parent)
        {
            setMinimumSize(640, 360);
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }

        void setData(const VideoReviewRequest *request, const QSet<int> *excluded)
        {
            request_ = request;
            excluded_ = excluded;
        }

        void setFrame(int frame, QImage image)
        {
            frame_ = frame;
            image_ = std::move(image);
            update();
        }

        void setToggleCallback(std::function<void(int)> callback)
        {
            toggleTrack_ = std::move(callback);
        }

        void refresh() { update(); }

    protected:
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.fillRect(rect(), QColor("#111827"));
            if (request_ == nullptr || image_.isNull())
            {
                painter.setPen(QColor("#D1D5DB"));
                painter.drawText(rect(), Qt::AlignCenter, tr("Could not load this frame preview."));
                return;
            }

            const QSizeF fitted = image_.size().scaled(size(), Qt::KeepAspectRatio);
            const QRectF target((width() - fitted.width()) * 0.5,
                                (height() - fitted.height()) * 0.5,
                                fitted.width(), fitted.height());
            painter.drawImage(target, image_);

            const double sx = target.width() / request_->frameSize.width();
            const double sy = target.height() / request_->frameSize.height();
            for (const auto &track: request_->tracks)
            {
                const auto *box = boxAtFrame(track, frame_);
                if (box == nullptr)
                {
                    continue;
                }
                const QRectF screen(target.x() + box->rect.x() * sx,
                                    target.y() + box->rect.y() * sy,
                                    box->rect.width() * sx,
                                    box->rect.height() * sy);
                const bool excluded = excluded_ != nullptr && excluded_->contains(track.id);
                QColor color = excluded ? QColor("#9CA3AF") : QColor("#F59E0B");
                painter.setPen(QPen(color, excluded ? 2.0 : 3.0,
                                    excluded ? Qt::DashLine : Qt::SolidLine));
                QColor fill = color;
                fill.setAlpha(excluded ? 28 : 52);
                painter.fillRect(screen, fill);
                painter.drawRect(screen);
                painter.setPen(Qt::white);
                painter.drawText(screen.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignTop,
                                 tr("Track %1").arg(track.id));
            }
        }

        void mousePressEvent(QMouseEvent *event) override
        {
            if (request_ == nullptr || image_.isNull() || !toggleTrack_)
            {
                return;
            }
            const QSizeF fitted = image_.size().scaled(size(), Qt::KeepAspectRatio);
            const QRectF target((width() - fitted.width()) * 0.5,
                                (height() - fitted.height()) * 0.5,
                                fitted.width(), fitted.height());
            if (!target.contains(event->position()))
            {
                return;
            }
            const double sx = target.width() / request_->frameSize.width();
            const double sy = target.height() / request_->frameSize.height();
            for (auto it = request_->tracks.crbegin(); it != request_->tracks.crend(); ++it)
            {
                const auto *box = boxAtFrame(*it, frame_);
                if (box == nullptr)
                {
                    continue;
                }
                const QRectF screen(target.x() + box->rect.x() * sx,
                                    target.y() + box->rect.y() * sy,
                                    box->rect.width() * sx,
                                    box->rect.height() * sy);
                if (screen.contains(event->position()))
                {
                    toggleTrack_(it->id);
                    return;
                }
            }
        }

    private:
        const VideoReviewRequest *request_ = nullptr;
        const QSet<int> *excluded_ = nullptr;
        QImage image_;
        int frame_ = 0;
        std::function<void(int)> toggleTrack_;
    };

    class VideoTimeline final : public QSlider
    {
    public:
        explicit VideoTimeline(QWidget *parent = nullptr) : QSlider(Qt::Horizontal, parent)
        {
            setMinimumHeight(34);
        }

        void setData(const VideoReviewRequest *request, const QSet<int> *excluded)
        {
            request_ = request;
            excluded_ = excluded;
            update();
        }

    protected:
        void paintEvent(QPaintEvent *event) override
        {
            QSlider::paintEvent(event);
            if (request_ == nullptr || request_->frameCount < 2)
            {
                return;
            }
            QStyleOptionSlider option;
            initStyleOption(&option);
            const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option,
                                                         QStyle::SC_SliderGroove, this);
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, false);
            const int y = height() - 5;
            for (const auto &track: request_->tracks)
            {
                if (track.boxes.isEmpty())
                {
                    continue;
                }
                const double denominator = static_cast<double>(request_->frameCount - 1);
                const int x1 = groove.left() + static_cast<int>(std::lround(
                    track.boxes.front().frame / denominator * groove.width()));
                const int x2 = groove.left() + static_cast<int>(std::lround(
                    track.boxes.back().frame / denominator * groove.width()));
                const bool excluded = excluded_ != nullptr && excluded_->contains(track.id);
                painter.fillRect(QRect(x1, y, std::max(2, x2 - x1 + 1), 3),
                                 excluded ? QColor("#9CA3AF") : QColor("#F59E0B"));
            }
        }

    private:
        const VideoReviewRequest *request_ = nullptr;
        const QSet<int> *excluded_ = nullptr;
    };

    VideoReviewDialog::VideoReviewDialog(VideoReviewRequest request, QWidget *parent)
        : QDialog(parent), request_(std::move(request))
    {
        setWindowTitle(tr("Review video tracks — %1").arg(request_.sourceName));
        resize(1120, 720);
        setModal(true);

        auto *root = new QVBoxLayout(this);
        auto *hint = new QLabel(
            tr("Scrub the timeline and uncheck false detections. Changes apply to the entire "
               "track before the video is encoded."), this);
        hint->setWordWrap(true);
        root->addWidget(hint);

        auto *body = new QHBoxLayout();
        canvas_ = new VideoReviewCanvas(this);
        canvas_->setData(&request_, &excludedTrackIds_);
        canvas_->setToggleCallback([this](int id)
        {
            setTrackIncluded(id, excludedTrackIds_.contains(id));
        });
        body->addWidget(canvas_, 1);

        auto *side = new QVBoxLayout();
        summaryLabel_ = new QLabel(this);
        side->addWidget(summaryLabel_);
        trackList_ = new QListWidget(this);
        trackList_->setMinimumWidth(260);
        for (const auto &track: request_.tracks)
        {
            if (track.boxes.isEmpty())
            {
                continue;
            }
            auto *item = new QListWidgetItem(
                tr("Track %1  ·  %2–%3")
                    .arg(track.id)
                    .arg(formatTime(track.boxes.front().frame / request_.fps))
                    .arg(formatTime(track.boxes.back().frame / request_.fps)),
                trackList_);
            item->setData(Qt::UserRole, track.id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
        connect(trackList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item)
        {
            setTrackIncluded(item->data(Qt::UserRole).toInt(),
                             item->checkState() == Qt::Checked);
        });
        connect(trackList_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item)
        {
            const int id = item->data(Qt::UserRole).toInt();
            const auto it = std::find_if(request_.tracks.cbegin(), request_.tracks.cend(),
                                         [id](const VideoReviewTrack &track)
                                         { return track.id == id; });
            if (it != request_.tracks.cend() && !it->boxes.isEmpty())
            {
                timeline_->setValue(it->boxes.front().frame);
            }
        });
        side->addWidget(trackList_, 1);

        auto *selectionButtons = new QHBoxLayout();
        auto *includeAll = new QPushButton(tr("Include all"), this);
        auto *excludeAll = new QPushButton(tr("Exclude all"), this);
        selectionButtons->addWidget(includeAll);
        selectionButtons->addWidget(excludeAll);
        side->addLayout(selectionButtons);
        connect(includeAll, &QPushButton::clicked, this, [this]
        {
            const QSignalBlocker blocker(trackList_);
            excludedTrackIds_.clear();
            for (int i = 0; i < trackList_->count(); ++i)
            {
                trackList_->item(i)->setCheckState(Qt::Checked);
            }
            canvas_->refresh();
            timeline_->update();
            updateSummary();
        });
        connect(excludeAll, &QPushButton::clicked, this, [this]
        {
            const QSignalBlocker blocker(trackList_);
            for (const auto &track: request_.tracks)
            {
                excludedTrackIds_.insert(track.id);
            }
            for (int i = 0; i < trackList_->count(); ++i)
            {
                trackList_->item(i)->setCheckState(Qt::Unchecked);
            }
            canvas_->refresh();
            timeline_->update();
            updateSummary();
        });
        body->addLayout(side);
        root->addLayout(body, 1);

        auto *timeRow = new QHBoxLayout();
        timeline_ = new VideoTimeline(this);
        timeline_->setRange(0, std::max(0, request_.frameCount - 1));
        timeline_->setPageStep(std::max(1, static_cast<int>(std::lround(request_.fps))));
        timeline_->setData(&request_, &excludedTrackIds_);
        timeLabel_ = new QLabel(this);
        timeLabel_->setMinimumWidth(120);
        timeRow->addWidget(timeline_, 1);
        timeRow->addWidget(timeLabel_);
        root->addLayout(timeRow);

        seekTimer_ = new QTimer(this);
        seekTimer_->setSingleShot(true);
        seekTimer_->setInterval(120);
        connect(seekTimer_, &QTimer::timeout, this, &VideoReviewDialog::loadFramePreview);
        connect(timeline_, &QSlider::valueChanged, this, &VideoReviewDialog::setFrame);

        auto *buttons = new QDialogButtonBox(this);
        auto *cancel = buttons->addButton(tr("Cancel all"), QDialogButtonBox::RejectRole);
        auto *encode = buttons->addButton(tr("Encode video"), QDialogButtonBox::AcceptRole);
        connect(cancel, &QPushButton::clicked, this, &VideoReviewDialog::reject);
        connect(encode, &QPushButton::clicked, this, &QDialog::accept);
        root->addWidget(buttons);

        updateSummary();
        int initialFrame = 0;
        if (!request_.tracks.isEmpty() && !request_.tracks.front().boxes.isEmpty())
        {
            initialFrame = request_.tracks.front().boxes.front().frame;
        }
        timeline_->setValue(initialFrame);
        setFrame(initialFrame);
        loadFramePreview();
    }

    VideoReviewResult VideoReviewDialog::result() const
    {
        VideoReviewResult result;
        result.decision = decision_;
        result.excludedTrackIds = excludedTrackIds_.values();
        std::sort(result.excludedTrackIds.begin(), result.excludedTrackIds.end());
        return result;
    }

    void VideoReviewDialog::reject()
    {
        decision_ = VideoReviewDecision::CancelAll;
        QDialog::reject();
    }

    void VideoReviewDialog::setFrame(int frame)
    {
        currentFrame_ = std::clamp(frame, 0, std::max(0, request_.frameCount - 1));
        const double currentSeconds = request_.fps > 0.0 ? currentFrame_ / request_.fps : 0.0;
        const double totalSeconds = request_.fps > 0.0
                                        ? (request_.frameCount - 1) / request_.fps : 0.0;
        timeLabel_->setText(tr("%1 / %2")
                                .arg(formatTime(currentSeconds), formatTime(totalSeconds)));
        if (frameCache_.contains(currentFrame_))
        {
            seekTimer_->stop();
            canvas_->setFrame(currentFrame_, frameCache_.value(currentFrame_));
        }
        else
        {
            seekTimer_->start();
        }
    }

    void VideoReviewDialog::loadFramePreview()
    {
        const int requestedFrame = currentFrame_;
        QImage image = extractPreviewFrame(request_, requestedFrame);
        if (requestedFrame != currentFrame_)
        {
            return;
        }
        if (!image.isNull())
        {
            if (frameCache_.size() >= 16)
            {
                frameCache_.erase(frameCache_.begin());
            }
            frameCache_.insert(requestedFrame, image);
        }
        canvas_->setFrame(requestedFrame, std::move(image));
    }

    void VideoReviewDialog::setTrackIncluded(int id, bool included)
    {
        if (included)
        {
            excludedTrackIds_.remove(id);
        }
        else
        {
            excludedTrackIds_.insert(id);
        }
        syncTrackItem(id);
        canvas_->refresh();
        timeline_->update();
        updateSummary();
    }

    void VideoReviewDialog::syncTrackItem(int id)
    {
        const QSignalBlocker blocker(trackList_);
        for (int i = 0; i < trackList_->count(); ++i)
        {
            auto *item = trackList_->item(i);
            if (item->data(Qt::UserRole).toInt() == id)
            {
                item->setCheckState(excludedTrackIds_.contains(id)
                                        ? Qt::Unchecked : Qt::Checked);
                break;
            }
        }
    }

    void VideoReviewDialog::updateSummary()
    {
        summaryLabel_->setText(tr("%1 of %2 tracks included")
                                   .arg(request_.tracks.size() - excludedTrackIds_.size())
                                   .arg(request_.tracks.size()));
    }
}
