#include "redactly/VideoIo.hpp"

#include "redactly/PathUtil.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <mutex>

namespace redactly
{
    namespace
    {
        constexpr int kProcessStartTimeoutMs = 15000;
        constexpr int kProcessIoTimeoutMs = 60000;
        constexpr int kProcessFinishTimeoutMs = 300000;

        QString trVideo(const char *text)
        {
            return QCoreApplication::translate("redactly::VideoIo", text);
        }

        QString executableName(const QString &base)
        {
#ifdef _WIN32
            return base + ".exe";
#else
            return base;
#endif
        }

        bool verifyBundledChecksum(const QString &toolPath, QString *error)
        {
            const QString manifestPath = toolPath + ".sha256";
            if (!QFileInfo::exists(manifestPath))
            {
                return true;
            }

            QFile manifest(manifestPath);
            if (!manifest.open(QIODevice::ReadOnly))
            {
                if (error)
                {
                    *error = trVideo("Could not read the FFmpeg checksum manifest.");
                }
                return false;
            }
            const QString expected =
                    QString::fromLatin1(manifest.readAll()).trimmed().section(' ', 0, 0);

            QFile tool(toolPath);
            if (!tool.open(QIODevice::ReadOnly))
            {
                if (error)
                {
                    *error = trVideo("Could not read the bundled FFmpeg binary.");
                }
                return false;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&tool))
            {
                if (error)
                {
                    *error = trVideo("Could not read the bundled FFmpeg binary.");
                }
                return false;
            }
            const QString actual = QString::fromLatin1(hash.result().toHex());
            if (actual.compare(expected, Qt::CaseInsensitive) != 0)
            {
                if (error)
                {
                    *error = trVideo("The bundled FFmpeg binary failed its integrity check.");
                }
                return false;
            }
            return true;
        }

        struct LocatedTool
        {
            QString path;
            bool bundled = false;
        };

        LocatedTool locateTool(const QString &baseName)
        {
            const QString exe = executableName(baseName);
            if (QCoreApplication::instance() != nullptr)
            {
                const auto appDir = QCoreApplication::applicationDirPath();
                const std::array<QString, 3> candidates = {
                    appDir + "/" + exe,
                    appDir + "/ffmpeg/" + exe,
                    appDir + "/../Resources/ffmpeg/" + exe,
                };
                for (const auto &candidate: candidates)
                {
                    const QFileInfo info(QDir::cleanPath(candidate));
                    if (info.exists() && info.isFile() && info.isExecutable())
                    {
                        return {info.absoluteFilePath(), true};
                    }
                }
            }

            QString found = QStandardPaths::findExecutable(exe);
            if (found.isEmpty())
            {
#if defined(Q_OS_MACOS)
                const QStringList fallbackDirs = {
                    "/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin"};
#elif defined(Q_OS_UNIX)
                const QStringList fallbackDirs = {"/usr/local/bin", "/usr/bin"};
#else
                const QStringList fallbackDirs;
#endif
                if (!fallbackDirs.isEmpty())
                {
                    found = QStandardPaths::findExecutable(exe, fallbackDirs);
                }
            }
            return {found, false};
        }

        QString probeVersionLine(const QString &ffmpegPath)
        {
            QProcess process;
            process.start(ffmpegPath, {"-version"});
            if (!process.waitForStarted(kProcessStartTimeoutMs) ||
                !process.waitForFinished(kProcessStartTimeoutMs))
            {
                process.kill();
                return {};
            }
            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            {
                return {};
            }
            const QString output = QString::fromUtf8(process.readAllStandardOutput());
            return output.section('\n', 0, 0).trimmed();
        }

        bool parseRational(const QString &value, int &num, int &den)
        {
            const auto parts = value.split('/');
            bool okNum = false;
            bool okDen = true;
            int parsedNum = parts.value(0).toInt(&okNum);
            int parsedDen = 1;
            if (parts.size() > 1)
            {
                parsedDen = parts.value(1).toInt(&okDen);
            }
            if (!okNum || !okDen || parsedNum <= 0 || parsedDen <= 0)
            {
                return false;
            }
            num = parsedNum;
            den = parsedDen;
            return true;
        }

        int normalizedRotation(double rotation)
        {
            int value = static_cast<int>(std::lround(rotation)) % 360;
            if (value < 0)
            {
                value += 360;
            }
            return value;
        }

        QString uniqueVideoTempPath(const QString &destination)
        {
            const QFileInfo info(destination);
            const auto suffix = QRandomGenerator::global()->generate64();
            const QString name = info.completeBaseName() + ".redactly-" +
                                 QString::number(suffix, 16) + "." + info.suffix();
            const QString dir = info.absolutePath();
            return dir.isEmpty() ? name : dir + "/" + name;
        }

        QString processErrorDetail(QProcess &process)
        {
            const QString stderrText =
                    QString::fromUtf8(process.readAllStandardError()).trimmed();
            if (!stderrText.isEmpty())
            {
                const auto lines = stderrText.split('\n', Qt::SkipEmptyParts);
                return lines.mid(std::max(0, static_cast<int>(lines.size()) - 3)).join(' ').trimmed();
            }
            return process.errorString();
        }

        QStringList hardwareEncoderCandidates()
        {
#if defined(_WIN32)
            return {QStringLiteral("h264_nvenc"), QStringLiteral("h264_amf"),
                    QStringLiteral("h264_qsv")};
#else
            return {};
#endif
        }

        QStringList videoEncoderArgs(const QString &encoder, const int crf)
        {
            if (encoder == QLatin1String("h264_nvenc"))
            {
                return {"-c:v", "h264_nvenc", "-preset", "p6", "-rc", "vbr",
                        "-cq", QString::number(crf), "-b:v", "0"};
            }
            if (encoder == QLatin1String("h264_amf"))
            {
                return {"-c:v", "h264_amf", "-quality", "quality", "-rc", "qvbr",
                        "-qvbr_quality_level", QString::number(crf)};
            }
            if (encoder == QLatin1String("h264_qsv"))
            {
                return {"-c:v", "h264_qsv", "-preset", "slow",
                        "-global_quality", QString::number(crf)};
            }
            return {"-c:v", "libx264", "-preset", "medium",
                    "-crf", QString::number(crf)};
        }

        bool encoderWorks(const FfmpegTools &tools, const QString &encoder,
                          const int width, const int height)
        {
            QStringList arguments = {
                "-v", "error", "-nostdin",
                "-f", "lavfi",
                "-i", QString("color=black:size=%1x%2:rate=30").arg(width).arg(height),
                "-frames:v", "3",
                "-pix_fmt", "yuv420p",
            };
            arguments << videoEncoderArgs(encoder, crfForQuality(VideoQuality::Balanced))
                      << "-f" << "null" << "-";

            QProcess probe;
            probe.start(tools.ffmpegPath, arguments);
            if (!probe.waitForStarted(kProcessStartTimeoutMs))
            {
                return false;
            }
            if (!probe.waitForFinished(kProcessIoTimeoutMs))
            {
                probe.kill();
                probe.waitForFinished(kProcessStartTimeoutMs);
                return false;
            }
            return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
        }

        QString selectVideoEncoder(const FfmpegTools &tools, const int width, const int height)
        {
            static std::mutex cacheMutex;
            static QHash<QString, bool> cache;

            std::lock_guard lock(cacheMutex);
            for (const auto &encoder : hardwareEncoderCandidates())
            {
                const QString key =
                        QString("%1:%2x%3").arg(encoder).arg(width).arg(height);
                auto it = cache.find(key);
                if (it == cache.end())
                {
                    it = cache.insert(key, encoderWorks(tools, encoder, width, height));
                    spdlog::info("Hardware video encoder {} {} at {}x{}",
                                 encoder.toStdString(),
                                 it.value() ? "available" : "unavailable", width, height);
                }
                if (it.value())
                {
                    return encoder;
                }
            }
            return QStringLiteral("libx264");
        }
    }

    std::optional<FfmpegTools> locateFfmpegTools(QString *error)
    {
        static std::mutex cacheMutex;
        static std::optional<FfmpegTools> cached;

        std::lock_guard lock(cacheMutex);
        if (cached)
        {
            return cached;
        }

        const auto fail = [&](const QString &message) -> std::optional<FfmpegTools>
        {
            if (error)
            {
                *error = message;
            }
            return std::nullopt;
        };

        const auto ffmpeg = locateTool("ffmpeg");
        const auto ffprobe = locateTool("ffprobe");
        if (ffmpeg.path.isEmpty() || ffprobe.path.isEmpty())
        {
            return fail(trVideo("FFmpeg was not found. Video processing is unavailable."));
        }

        QString checksumError;
        if (ffmpeg.bundled && !verifyBundledChecksum(ffmpeg.path, &checksumError))
        {
            return fail(checksumError);
        }
        if (ffprobe.bundled && !verifyBundledChecksum(ffprobe.path, &checksumError))
        {
            return fail(checksumError);
        }

        const QString version = probeVersionLine(ffmpeg.path);
        if (version.isEmpty())
        {
            return fail(trVideo("FFmpeg was found but could not be executed."));
        }

        FfmpegTools tools;
        tools.ffmpegPath = ffmpeg.path;
        tools.ffprobePath = ffprobe.path;
        tools.bundled = ffmpeg.bundled;
        tools.versionLine = version;
        spdlog::info("FFmpeg: {} ({})", version.toStdString(),
                     tools.bundled ? "bundled" : "system");
        cached = tools;
        return cached;
    }

    bool isSupportedVideo(const std::filesystem::path &path)
    {
        auto extension = pathToUtf8(path.extension());
        std::ranges::transform(extension, extension.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        return extension == ".mp4" || extension == ".mov" || extension == ".m4v";
    }

    int crfForQuality(const VideoQuality quality)
    {
        switch (quality)
        {
            case VideoQuality::Balanced:
                return 21;
            case VideoQuality::SpaceSaver:
                return 24;
            case VideoQuality::HighQuality:
                break;
        }
        return 18;
    }

    int VideoInfo::displayWidth() const
    {
        return (rotation == 90 || rotation == 270) ? height : width;
    }

    int VideoInfo::displayHeight() const
    {
        return (rotation == 90 || rotation == 270) ? width : height;
    }

    double VideoInfo::fps() const
    {
        return fpsDen > 0 ? static_cast<double>(fpsNum) / fpsDen : 0.0;
    }

    std::optional<VideoInfo> probeVideo(const FfmpegTools &tools,
                                        const QString &path,
                                        QString *error)
    {
        QProcess process;
        process.start(tools.ffprobePath,
                      {"-v", "error", "-print_format", "json",
                       "-show_streams", "-show_format", path});
        if (!process.waitForStarted(kProcessStartTimeoutMs) ||
            !process.waitForFinished(kProcessIoTimeoutMs))
        {
            process.kill();
            if (error)
            {
                *error = trVideo("Could not inspect the video (ffprobe did not respond).");
            }
            return std::nullopt;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        {
            if (error)
            {
                *error = trVideo("Could not inspect the video: %1")
                        .arg(processErrorDetail(process));
            }
            return std::nullopt;
        }

        const auto document = QJsonDocument::fromJson(process.readAllStandardOutput());
        const auto streams = document.object().value("streams").toArray();
        const auto format = document.object().value("format").toObject();

        VideoInfo info;
        bool videoFound = false;
        for (const auto &entry: streams)
        {
            const auto stream = entry.toObject();
            const auto type = stream.value("codec_type").toString();
            if (type == "audio" && !info.hasAudio)
            {
                info.hasAudio = true;
                info.audioCodec = stream.value("codec_name").toString();
                continue;
            }
            if (type != "video" || videoFound)
            {
                continue;
            }
            videoFound = true;

            info.videoCodec = stream.value("codec_name").toString();
            info.pixelFormat = stream.value("pix_fmt").toString();
            info.colorTransfer = stream.value("color_transfer").toString();
            info.width = stream.value("width").toInt();
            info.height = stream.value("height").toInt();

            int avgNum = 0;
            int avgDen = 1;
            const bool avgValid =
                    parseRational(stream.value("avg_frame_rate").toString(), avgNum, avgDen);
            int rNum = 0;
            int rDen = 1;
            const bool rValid =
                    parseRational(stream.value("r_frame_rate").toString(), rNum, rDen);
            if (avgValid)
            {
                info.fpsNum = avgNum;
                info.fpsDen = avgDen;
            }
            else if (rValid)
            {
                info.fpsNum = rNum;
                info.fpsDen = rDen;
            }
            info.isVfr = avgValid && rValid &&
                         static_cast<qint64>(avgNum) * rDen != static_cast<qint64>(rNum) * avgDen;

            for (const auto &sideDataEntry: stream.value("side_data_list").toArray())
            {
                const auto sideData = sideDataEntry.toObject();
                if (sideData.contains("rotation"))
                {
                    info.rotation = normalizedRotation(sideData.value("rotation").toDouble());
                }
            }
            const auto rotateTag = stream.value("tags").toObject().value("rotate").toString();
            if (info.rotation == 0 && !rotateTag.isEmpty())
            {
                info.rotation = normalizedRotation(rotateTag.toDouble());
            }

            bool durationOk = false;
            const double streamDuration =
                    stream.value("duration").toString().toDouble(&durationOk);
            if (durationOk && streamDuration > 0)
            {
                info.durationSeconds = streamDuration;
            }
            const auto frames = stream.value("nb_frames").toString().toLongLong();
            if (frames > 0)
            {
                info.estimatedFrameCount = frames;
            }
        }

        if (info.durationSeconds <= 0)
        {
            info.durationSeconds = format.value("duration").toString().toDouble();
        }
        if (info.estimatedFrameCount <= 0 && info.durationSeconds > 0 && info.fps() > 0)
        {
            info.estimatedFrameCount =
                    static_cast<qint64>(std::llround(info.durationSeconds * info.fps()));
        }

        if (!videoFound)
        {
            if (error)
            {
                *error = trVideo("The file contains no video stream.");
            }
            return std::nullopt;
        }
        return info;
    }

    QString videoUnsupportedReason(const VideoInfo &info)
    {
        if (info.width <= 0 || info.height <= 0 || info.fpsNum <= 0)
        {
            return trVideo("the video stream could not be read");
        }
        if (info.videoCodec != "h264" && info.videoCodec != "hevc")
        {
            return trVideo("unsupported video codec '%1' (H.264/HEVC only)")
                    .arg(info.videoCodec);
        }
        static const QRegularExpression highBitDepth("(9|10|12|14|16)(le|be)?$");
        if (highBitDepth.match(info.pixelFormat).hasMatch())
        {
            return trVideo("10-bit or higher bit depth is not supported yet");
        }
        if (info.colorTransfer == "smpte2084" || info.colorTransfer == "arib-std-b67")
        {
            return trVideo("HDR video is not supported yet");
        }
        return {};
    }

    VideoFrameReader::VideoFrameReader() = default;

    VideoFrameReader::~VideoFrameReader()
    {
        close();
    }

    bool VideoFrameReader::open(const FfmpegTools &tools, const QString &path,
                                const VideoInfo &info, const int decodeLongEdge)
    {
        close();
        error_.clear();
        atEnd_ = false;
        frameWidth_ = info.displayWidth();
        frameHeight_ = info.displayHeight();
        if (frameWidth_ <= 0 || frameHeight_ <= 0 || info.fpsNum <= 0)
        {
            error_ = trVideo("Invalid video dimensions.");
            return false;
        }

        QString filter = QString("fps=%1/%2").arg(info.fpsNum).arg(info.fpsDen);
        const int longEdge = std::max(frameWidth_, frameHeight_);
        if (decodeLongEdge > 0 && decodeLongEdge < longEdge)
        {
            const double scale = static_cast<double>(decodeLongEdge) / longEdge;
            frameWidth_ = std::max(2, static_cast<int>(std::lround(frameWidth_ * scale)));
            frameHeight_ = std::max(2, static_cast<int>(std::lround(frameHeight_ * scale)));
            filter += QString(",scale=%1:%2:flags=area").arg(frameWidth_).arg(frameHeight_);
        }

        process_ = std::make_unique<QProcess>();
        process_->start(tools.ffmpegPath,
                        {"-v", "error", "-nostdin",
                         "-i", path,
                         "-map", "0:v:0",
                         "-vf", filter,
                         "-f", "rawvideo",
                         "-pix_fmt", "bgr24",
                         "-"});
        if (!process_->waitForStarted(kProcessStartTimeoutMs))
        {
            error_ = trVideo("Could not start FFmpeg for decoding.");
            process_.reset();
            return false;
        }
        return true;
    }

    int VideoFrameReader::frameWidth() const
    {
        return frameWidth_;
    }

    int VideoFrameReader::frameHeight() const
    {
        return frameHeight_;
    }

    bool VideoFrameReader::readFrame(cv::Mat &frame)
    {
        if (!process_ || atEnd_)
        {
            return false;
        }

        const qint64 frameBytes =
                static_cast<qint64>(frameWidth_) * frameHeight_ * 3;
        frame.create(frameHeight_, frameWidth_, CV_8UC3);

        qint64 received = 0;
        while (received < frameBytes)
        {
            const qint64 chunk = process_->read(
                reinterpret_cast<char *>(frame.data) + received, frameBytes - received);
            if (chunk > 0)
            {
                received += chunk;
                continue;
            }
            if (chunk < 0 && process_->state() == QProcess::Running)
            {
                error_ = trVideo("Decoding failed: %1").arg(processErrorDetail(*process_));
                atEnd_ = true;
                return false;
            }
            if (process_->state() == QProcess::NotRunning &&
                process_->bytesAvailable() == 0)
            {
                atEnd_ = true;
                if (received > 0)
                {
                    error_ = trVideo("Decoding ended mid-frame: %1")
                            .arg(processErrorDetail(*process_));
                }
                else if (process_->exitStatus() != QProcess::NormalExit ||
                         process_->exitCode() != 0)
                {
                    error_ = trVideo("Decoding failed: %1").arg(processErrorDetail(*process_));
                }
                return false;
            }
            if (!process_->waitForReadyRead(kProcessIoTimeoutMs) &&
                process_->state() == QProcess::Running)
            {
                error_ = trVideo("Decoding timed out.");
                atEnd_ = true;
                return false;
            }
        }
        return true;
    }

    void VideoFrameReader::close()
    {
        if (process_)
        {
            if (process_->state() != QProcess::NotRunning)
            {
                process_->kill();
                process_->waitForFinished(kProcessStartTimeoutMs);
            }
            process_.reset();
        }
        atEnd_ = true;
    }

    bool VideoFrameReader::atEnd() const
    {
        return atEnd_;
    }

    QString VideoFrameReader::errorString() const
    {
        return error_;
    }

    VideoFrameWriter::VideoFrameWriter() = default;

    VideoFrameWriter::~VideoFrameWriter()
    {
        abort();
    }

    bool VideoFrameWriter::open(const FfmpegTools &tools,
                                const QString &destination,
                                const QString &audioSource,
                                const VideoInfo &info,
                                const int crf,
                                const bool hardwareEncoder)
    {
        abort();
        error_.clear();
        frameWidth_ = info.displayWidth();
        frameHeight_ = info.displayHeight();
        if (frameWidth_ <= 0 || frameHeight_ <= 0 || info.fpsNum <= 0)
        {
            error_ = trVideo("Invalid video dimensions.");
            return false;
        }

        encoderName_ = QStringLiteral("libx264");
        if (hardwareEncoder)
        {
            encoderName_ = selectVideoEncoder(tools,
                                              frameWidth_ - (frameWidth_ % 2),
                                              frameHeight_ - (frameHeight_ % 2));
        }
        spdlog::info("Video encoder: {}", encoderName_.toStdString());

        destinationPath_ = destination;
        tempPath_ = uniqueVideoTempPath(destination);

        static const QStringList mp4CompatibleAudio = {"aac", "mp3", "ac3", "eac3", "alac"};
        const bool copyAudio = info.hasAudio && mp4CompatibleAudio.contains(info.audioCodec);

        QStringList arguments = {
            "-v", "error", "-y",
            "-f", "rawvideo",
            "-pix_fmt", "bgr24",
            "-s", QString("%1x%2").arg(frameWidth_).arg(frameHeight_),
            "-framerate", QString("%1/%2").arg(info.fpsNum).arg(info.fpsDen),
            "-i", "-",
        };
        if (info.hasAudio && info.durationSeconds > 0)
        {
            arguments << "-t" << QString::number(info.durationSeconds, 'f', 3);
        }
        arguments << "-i" << audioSource
                  << "-map" << "0:v:0"
                  << "-map" << "1:a:0?"
                  << videoEncoderArgs(encoderName_, crf)
                  << "-pix_fmt" << "yuv420p";
        if ((frameWidth_ % 2) != 0 || (frameHeight_ % 2) != 0)
        {
            arguments << "-vf"
                      << QString("crop=%1:%2:0:0")
                             .arg(frameWidth_ - (frameWidth_ % 2))
                             .arg(frameHeight_ - (frameHeight_ % 2));
        }
        if (info.hasAudio)
        {
            if (copyAudio)
            {
                arguments << "-c:a" << "copy";
            }
            else
            {
                arguments << "-c:a" << "aac" << "-b:a" << "192k";
            }
        }
        arguments << "-map_metadata" << "-1"
                  << "-map_chapters" << "-1"
                  << "-movflags" << "+faststart"
                  << "-f" << "mp4"
                  << tempPath_;

        process_ = std::make_unique<QProcess>();
        process_->start(tools.ffmpegPath, arguments);
        if (!process_->waitForStarted(kProcessStartTimeoutMs))
        {
            error_ = trVideo("Could not start FFmpeg for encoding.");
            process_.reset();
            return false;
        }
        return true;
    }

    bool VideoFrameWriter::writeFrame(const cv::Mat &frame)
    {
        if (!process_ || process_->state() != QProcess::Running)
        {
            error_ = trVideo("Encoding failed: %1")
                    .arg(process_ ? processErrorDetail(*process_) : QString());
            return false;
        }
        if (frame.cols != frameWidth_ || frame.rows != frameHeight_ ||
            frame.type() != CV_8UC3)
        {
            error_ = trVideo("Internal error: frame does not match the video format.");
            return false;
        }

        cv::Mat continuous = frame;
        if (!frame.isContinuous())
        {
            continuous = frame.clone();
        }

        const qint64 frameBytes =
                static_cast<qint64>(frameWidth_) * frameHeight_ * 3;
        qint64 written = 0;
        while (written < frameBytes)
        {
            const qint64 chunk = process_->write(
                reinterpret_cast<const char *>(continuous.data) + written,
                frameBytes - written);
            if (chunk < 0 || process_->state() != QProcess::Running)
            {
                error_ = trVideo("Encoding failed: %1").arg(processErrorDetail(*process_));
                return false;
            }
            written += chunk;
            while (process_->bytesToWrite() > 0)
            {
                if (!process_->waitForBytesWritten(kProcessIoTimeoutMs))
                {
                    error_ = trVideo("Encoding failed: %1")
                            .arg(processErrorDetail(*process_));
                    return false;
                }
            }
        }
        return true;
    }

    bool VideoFrameWriter::finish()
    {
        if (!process_)
        {
            return false;
        }

        process_->closeWriteChannel();
        if (!process_->waitForFinished(kProcessFinishTimeoutMs))
        {
            error_ = trVideo("Encoding timed out while finalizing.");
            abort();
            return false;
        }
        const bool ok = process_->exitStatus() == QProcess::NormalExit &&
                        process_->exitCode() == 0;
        if (!ok)
        {
            error_ = trVideo("Encoding failed: %1").arg(processErrorDetail(*process_));
            abort();
            return false;
        }
        process_.reset();

        QFile::remove(destinationPath_);
        if (!QFile::rename(tempPath_, destinationPath_))
        {
            if (!QFile::copy(tempPath_, destinationPath_))
            {
                QFile::remove(tempPath_);
                error_ = trVideo("Could not move the finished video into place.");
                return false;
            }
            QFile::remove(tempPath_);
        }
        tempPath_.clear();
        return true;
    }

    void VideoFrameWriter::abort()
    {
        if (process_)
        {
            if (process_->state() != QProcess::NotRunning)
            {
                process_->kill();
                process_->waitForFinished(kProcessStartTimeoutMs);
            }
            process_.reset();
        }
        if (!tempPath_.isEmpty())
        {
            QFile::remove(tempPath_);
            tempPath_.clear();
        }
    }

    QString VideoFrameWriter::errorString() const
    {
        return error_;
    }

    QString VideoFrameWriter::encoderName() const
    {
        return encoderName_;
    }
}
