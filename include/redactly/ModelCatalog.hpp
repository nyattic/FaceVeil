#pragma once

#include <QString>

#include <array>

namespace redactly
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

    const std::array<BuiltinModel, 1> &builtinModels();
    const BuiltinModel &plateModel();
    const BuiltinModel *findBuiltinModel(const QString &path);

    QString modelCacheDir();
    QString firstExistingModelPath(const QString &fileName);
}
