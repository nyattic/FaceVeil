#pragma once

#include <QString>

#include <filesystem>
#include <string>

namespace cloakframe
{
    inline std::filesystem::path pathFromQString(const QString &value)
    {
#ifdef _WIN32
        return std::filesystem::path(value.toStdWString());
#else
        return std::filesystem::path(value.toStdString());
#endif
    }

    inline std::string pathToUtf8(const std::filesystem::path &value)
    {
        const auto utf8 = value.u8string();
        return std::string(utf8.begin(), utf8.end());
    }

    inline QString pathToQString(const std::filesystem::path &value)
    {
        const auto utf8 = value.u8string();
        return QString::fromUtf8(reinterpret_cast<const char *>(utf8.data()),
                                 static_cast<int>(utf8.size()));
    }
}
