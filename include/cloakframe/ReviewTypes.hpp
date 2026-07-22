#pragma once

#include <QMetaType>
#include <QRectF>
#include <QVector>

namespace cloakframe
{
    enum class ReviewDecision
    {
        Save,
        DoNotSave,
        CopyOriginal,
        CancelAll,
    };

    struct ReviewResult
    {
        ReviewDecision decision = ReviewDecision::Save;
        QVector<QRectF> finalBoxes;
    };
}

Q_DECLARE_METATYPE(cloakframe::ReviewResult)
