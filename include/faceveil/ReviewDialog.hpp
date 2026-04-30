#pragma once

#include "faceveil/ReviewTypes.hpp"

#include <QDialog>
#include <QImage>
#include <QRectF>
#include <QVector>

class QLabel;
class QMouseEvent;
class QPaintEvent;
class QPushButton;

namespace faceveil
{
    class ReviewCanvas;

    class ReviewDialog final : public QDialog
    {
        Q_OBJECT

    public:
        ReviewDialog(const QImage &image,
                     const QString &sourceName,
                     const QVector<QRectF> &detected,
                     int currentIndex,
                     int total,
                     QWidget *parent = nullptr);

        [[nodiscard]] ReviewResult result() const;

        void reject() override;

    private:
        ReviewCanvas *canvas_ = nullptr;
        QLabel *hintLabel_ = nullptr;
        ReviewDecision decision_ = ReviewDecision::Save;
    };
}
