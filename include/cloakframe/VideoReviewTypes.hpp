#pragma once

#include <QMetaType>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QVector>

namespace cloakframe
{
    struct VideoReviewBox
    {
        int frame = 0;
        QRectF rect;
        bool interpolated = false;
    };

    struct VideoReviewTrack
    {
        int id = 0;
        QVector<VideoReviewBox> boxes;
    };

    struct VideoReviewRequest
    {
        QString sourcePath;
        QString ffmpegPath;
        QString sourceName;
        QSize frameSize;
        double fps = 0.0;
        int frameCount = 0;
        QVector<VideoReviewTrack> tracks;
    };

    enum class VideoReviewDecision
    {
        Encode,
        CancelAll,
    };

    struct VideoReviewResult
    {
        VideoReviewDecision decision = VideoReviewDecision::Encode;
        QVector<int> excludedTrackIds;
    };
}

Q_DECLARE_METATYPE(cloakframe::VideoReviewBox)
Q_DECLARE_METATYPE(cloakframe::VideoReviewTrack)
Q_DECLARE_METATYPE(cloakframe::VideoReviewRequest)
Q_DECLARE_METATYPE(cloakframe::VideoReviewResult)
