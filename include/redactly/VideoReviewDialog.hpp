#pragma once

#include "redactly/VideoReviewTypes.hpp"

#include <QDialog>
#include <QHash>
#include <QImage>
#include <QSet>

class QLabel;
class QListWidget;
class QTimer;

namespace redactly
{
    class VideoReviewCanvas;
    class VideoTimeline;

    class VideoReviewDialog final : public QDialog
    {
        Q_OBJECT

    public:
        explicit VideoReviewDialog(VideoReviewRequest request, QWidget *parent = nullptr);

        [[nodiscard]] VideoReviewResult result() const;

        void reject() override;

    private:
        void setFrame(int frame);
        void loadFramePreview();
        void setTrackIncluded(int id, bool included);
        void syncTrackItem(int id);
        void updateSummary();

        VideoReviewRequest request_;
        QSet<int> excludedTrackIds_;
        QHash<int, QImage> frameCache_;
        VideoReviewCanvas *canvas_ = nullptr;
        VideoTimeline *timeline_ = nullptr;
        QListWidget *trackList_ = nullptr;
        QLabel *timeLabel_ = nullptr;
        QLabel *summaryLabel_ = nullptr;
        QTimer *seekTimer_ = nullptr;
        int currentFrame_ = 0;
        VideoReviewDecision decision_ = VideoReviewDecision::Encode;
    };
}
