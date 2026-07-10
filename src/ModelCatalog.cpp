#include "redactly/ModelCatalog.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace redactly
{
    QString modelCacheDir()
    {
        const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const auto root = base.isEmpty() ? QDir::homePath() : base;
        return root + "/Redactly/models";
    }

    QString firstExistingModelPath(const QString &fileName)
    {
        const auto appDir = QCoreApplication::applicationDirPath();
        const std::array<QString, 5> candidates = {
            modelCacheDir() + "/" + fileName,
            appDir + "/models/" + fileName,
            appDir + "/../Resources/models/" + fileName,
            appDir + "/../../../../models/" + fileName,
            QDir::currentPath() + "/models/" + fileName,
        };

        for (const auto &candidate: candidates)
        {
            const QFileInfo info(QDir::cleanPath(candidate));
            if (info.exists() && info.isFile())
            {
                return info.absoluteFilePath();
            }
        }

        return {};
    }

    const std::array<BuiltinModel, 1> &builtinModels()
    {
        static const std::array<BuiltinModel, 1> models = {
            BuiltinModel{
                "YuNet  ·  OpenCV", "face_detection_yunet_2026may.onnx",
                "https://media.githubusercontent.com/media/opencv/opencv_zoo/main/"
                "models/face_detection_yunet/"
                "face_detection_yunet_2026may.onnx",
                "ebafce4e3c118d6554634be5c27ab333b4c047a9a8c3faf1d7cf93101c22f0f0",
                229738},
        };
        return models;
    }

    const BuiltinModel &plateModel()
    {
        static const BuiltinModel model{
            "License plates  ·  YOLOv9-t",
            "yolo-v9-t-512-license-plates-end2end.onnx",
            "https://github.com/ankandrew/open-image-models/releases/download/assets/"
            "yolo-v9-t-512-license-plates-end2end.onnx",
            "746fdd358ec110418775d7c9d8d07910d48b1a21471f92bf4421f6510d6daade", 7799480};
        return model;
    }

    const BuiltinModel *findBuiltinModel(const QString &path)
    {
        const auto name = QFileInfo(path).fileName();
        for (const auto &model: builtinModels())
        {
            if (model.fileName == name)
            {
                return &model;
            }
        }
        return nullptr;
    }
}
