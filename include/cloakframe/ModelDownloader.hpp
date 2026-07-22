#pragma once

#include <QString>

class QWidget;

namespace cloakframe
{
    struct BuiltinModel;

    bool downloadModelWithProgress(QWidget *parent, const BuiltinModel &model, const QString &destPath);
    bool ensureBuiltinModelAvailable(QWidget *parent, const BuiltinModel &model, const QString &destPath);
    bool ensurePlateModelAvailable(QWidget *parent, const QString &destPath);
    bool customModelFileIsAllowed(QWidget *parent, const QString &path);
    bool confirmTrustedCustomModel(QWidget *parent, const QString &path);
}
