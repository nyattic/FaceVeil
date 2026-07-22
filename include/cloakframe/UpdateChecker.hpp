#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace cloakframe
{
    class UpdateChecker final : public QObject
    {
        Q_OBJECT

    public:
        explicit UpdateChecker(QString currentVersion, QObject *parent = nullptr);

        ~UpdateChecker() override;

        void check();

    signals:
        void updateAvailable(const QString &latestVersion, const QString &releaseUrl,
                             const QString &releaseNotes);

    private:
        QString currentVersion_;
        QNetworkAccessManager *manager_;
    };
}
