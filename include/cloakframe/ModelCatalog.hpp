#pragma once

#include <QByteArray>
#include <QString>

#include <array>

namespace cloakframe
{
    inline constexpr qint64 kMaxCustomModelBytes = 512LL * 1024LL * 1024LL;

    struct BuiltinModel
    {
        QString label;
        QString fileName;
        QString url;
        QString sha256;
        qint64 approxBytes;
    };

    const std::array<BuiltinModel, 2> &builtinModels();
    const BuiltinModel &plateModel();
    bool modelDigestMatches(const BuiltinModel &model, const QByteArray &digest);

    QString modelCacheDir();
    QString firstExistingModelPath(const QString &fileName);
}
