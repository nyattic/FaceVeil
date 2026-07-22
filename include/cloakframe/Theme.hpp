#pragma once

#include <QIcon>
#include <QString>

class QApplication;

namespace cloakframe
{
    enum class ThemeMode
    {
        System,
        Light,
        Dark
    };

    ThemeMode themeModeFromString(const QString &value);

    QString themeModeToString(ThemeMode mode);

    bool systemPrefersDark();

    void applyTheme(QApplication &app, ThemeMode mode);

    QIcon settingsGearIcon(ThemeMode mode);
}
