#include "redactly/UpdateChecker.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QVersionNumber>

#include <utility>

namespace redactly
{
    namespace
    {
        constexpr auto kReleasesApiUrl =
                "https://api.github.com/repos/nyattic/Redactly/releases/latest";
        constexpr auto kReleasesPageUrl =
                "https://github.com/nyattic/Redactly/releases/latest";

        QString stripVersionPrefix(QString value)
        {
            if (value.startsWith(QLatin1Char('v')) || value.startsWith(QLatin1Char('V')))
            {
                value.remove(0, 1);
            }
            return value;
        }
    }

    UpdateChecker::UpdateChecker(QString currentVersion, QObject *parent)
        : QObject(parent),
          currentVersion_(std::move(currentVersion)),
          manager_(new QNetworkAccessManager(this))
    {
    }

    UpdateChecker::~UpdateChecker() = default;

    void UpdateChecker::check()
    {
        QNetworkRequest request{QUrl(QString::fromLatin1(kReleasesApiUrl))};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("Redactly/%1").arg(currentVersion_));

        QNetworkReply *reply = manager_->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]
        {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError)
            {
                return;
            }

            const auto document = QJsonDocument::fromJson(reply->readAll());
            if (!document.isObject())
            {
                return;
            }
            const auto object = document.object();
            if (object.value("draft").toBool() || object.value("prerelease").toBool())
            {
                return;
            }

            const auto tag = object.value("tag_name").toString();
            if (tag.isEmpty())
            {
                return;
            }

            const auto latest = QVersionNumber::fromString(stripVersionPrefix(tag));
            const auto current = QVersionNumber::fromString(stripVersionPrefix(currentVersion_));
            if (latest.isNull() || QVersionNumber::compare(latest, current) <= 0)
            {
                return;
            }

            const auto url = object.value("html_url").toString();
            const auto releaseNotes = object.value("body").toString().trimmed();
            emit updateAvailable(tag,
                                 url.isEmpty() ? QString::fromLatin1(kReleasesPageUrl) : url,
                                 releaseNotes);
        });
    }
}
