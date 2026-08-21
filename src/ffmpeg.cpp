#include "ffmpeg.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace ffmpeg {

namespace {
// Upper bound on a synchronous probe so a hung/unresponsive file (e.g. a stalled
// network mount) can't freeze the caller — load() runs probe() on the UI thread.
constexpr int kProbeTimeoutMs = 15000;
// Poll granularity while waiting on a thumbnail child, so cancellation is prompt.
constexpr int kThumbPollMs = 50;

// Shared probe body: fetch the first stream matching `selector` plus duration.
VideoInfo probeStream(const QString &path, const QString &selector, const QString &missingMessage) {
    VideoInfo info;
    info.path = path;

    const QString ffprobe = toolPath("ffprobe");
    if (ffprobe.isEmpty()) {
        info.error = "`ffprobe` was not found on your PATH. Install ffmpeg.";
        return info;
    }

    QProcess proc;
    proc.start(ffprobe, {
        "-v", "error",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        "-select_streams", selector,
        path,
    });
    if (!proc.waitForFinished(kProbeTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(-1);
        info.error = "ffprobe timed out reading this file.";
        return info;
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        info.error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (info.error.isEmpty())
            info.error = "ffprobe failed.";
        return info;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    const QJsonObject root = doc.object();
    const QJsonArray streams = root.value("streams").toArray();
    if (streams.isEmpty()) {
        info.error = missingMessage;
        return info;
    }

    const QJsonObject stream = streams.first().toObject();
    info.width = stream.value("width").toInt();
    info.height = stream.value("height").toInt();

    // Duration can live on the stream or on the container.
    QString durationStr = stream.value("duration").toString();
    if (durationStr.isEmpty())
        durationStr = root.value("format").toObject().value("duration").toString();
    if (durationStr.isEmpty()) {
        info.error = "Could not determine the duration.";
        return info;
    }

    info.duration = durationStr.toDouble();
    info.ok = info.duration > 0.0;
    if (!info.ok)
        info.error = "The file has a zero or invalid duration.";
    return info;
}
}

QString toolPath(const QString &tool) {
    return QStandardPaths::findExecutable(tool);
}

VideoInfo probe(const QString &path) {
    return probeStream(path, QStringLiteral("v:0"),
                       QStringLiteral("No video stream found in this file."));
}

VideoInfo audioProbe(const QString &path) {
    return probeStream(path, QStringLiteral("a:0"),
                       QStringLiteral("No audio stream found in this file."));
}

bool hasAudioStream(const QString &path) {
    return probeStream(path, QStringLiteral("a:0"), QString()).ok;
}

QImage thumbnail(const QString &path, double time, int height,
                 const std::atomic<bool> *cancel) {
    const QString ffmpeg = toolPath("ffmpeg");
    if (ffmpeg.isEmpty())
        return {};

    QProcess proc;
    proc.start(ffmpeg, {
        "-loglevel", "error",
        "-ss", QString::number(qMax(time, 0.0), 'f', 3),
        "-i", path,
        "-frames:v", "1",
        "-vf", QString("scale=-1:%1").arg(height),
        "-f", "image2pipe",
        "-vcodec", "mjpeg",
        "pipe:1",
    });

    // Poll instead of waitForFinished(-1) so a cancel request can kill the child
    // promptly — otherwise the std::future destructor in ThumbWorker would block
    // the UI thread until ffmpeg finishes on its own.
    while (!proc.waitForFinished(kThumbPollMs)) {
        if (proc.state() == QProcess::NotRunning)
            break;  // failed to start, or exited between polls
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            proc.kill();
            proc.waitForFinished(-1);
            return {};
        }
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};

    const QByteArray data = proc.readAllStandardOutput();
    QImage img;
    img.loadFromData(data, "JPEG");
    return img;
}

QStringList trimArgs(const QString &src, const QString &dst, double start, double end,
                     int scaleHeight, const AudioSpec &audio) {
    // Machine-readable progress on stdout (errors stay on stderr), so the UI
    // can show how far along the encode is.
    QStringList args = {"-y", "-loglevel", "error", "-progress", "pipe:1"};
    // -ss before -i seeks fast; -t gives the output duration. +faststart puts
    // the moov atom up front so shared clips start playing before they finish
    // downloading.
    args << "-ss" << QString::number(start, 'f', 3)
         << "-i" << src;
    if (audio.valid())
        args << "-ss" << QString::number(audio.startSec, 'f', 3)
             << "-i" << audio.path;
    args << "-t" << QString::number(qMax(end - start, 0.0), 'f', 3);
    // Cap the shorter side, judged on the decoded (rotation-applied) frame, so
    // portrait and landscape both keep their aspect ratio. -2 keeps the other
    // side divisible by two, which libx264 requires.
    if (scaleHeight > 0)
        args << "-vf"
             << QString("scale='if(gt(iw,ih),-2,%1)':'if(gt(iw,ih),%1,-2)'").arg(scaleHeight);
    if (audio.valid())
        args << "-map" << "0:v:0" << "-map" << "1:a:0";
    args << "-c:v" << "libx264" << "-preset" << "veryfast"
         << "-crf" << "18" << "-c:a" << "aac";
    // Replacement audio shorter than the clip would otherwise truncate the video;
    // pad it with silence to the clip length instead.
    if (audio.valid())
        args << "-af" << "apad";
    args << "-movflags" << "+faststart"
         << dst;
    return args;
}

QStringList mergeClipArgs(const QString &src, const QString &dst, double start, double end,
                          int scaleHeight, const AudioSpec &audio) {
    QStringList args = trimArgs(src, dst, start, end, scaleHeight, audio);
    // Normalize for concat compatibility: a mix of source sample rates, channel
    // counts or pixel formats would make a stream-copy concat produce broken
    // audio or reject outright. Output options must precede the trailing dst.
    const QStringList normalize = {
        QStringLiteral("-ar"), QStringLiteral("48000"),
        QStringLiteral("-ac"), QStringLiteral("2"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
    };
    const int dstIndex = args.size() - 1;
    for (int i = normalize.size() - 1; i >= 0; --i)
        args.insert(dstIndex, normalize.at(i));
    return args;
}

QStringList concatArgs(const QString &listPath, const QString &dst) {
    return {
        QStringLiteral("-y"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("concat"),
        QStringLiteral("-safe"), QStringLiteral("0"),
        QStringLiteral("-i"), listPath,
        QStringLiteral("-c"), QStringLiteral("copy"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        dst,
    };
}

QStringList mixArgs(const QString &mergedSrc, const QString &dst,
                    const QList<AudioSpec> &clips, double totalDuration,
                    bool muteVideoAudio, bool mergedHasAudio) {
    QStringList args = {"-y", "-loglevel", "error", "-i", mergedSrc};
    for (const AudioSpec &clip : clips)
        args << "-i" << clip.path;

    // Every input is resampled to 48 kHz stereo so amix never rejects a clip for
    // a mismatched rate/layout; atrim/asetpts crop and reset timestamps, and
    // adelay positions the clip in milliseconds. The base is the merged video's
    // own audio when it has one (silenced when muted), otherwise a synthesized
    // silent track — sources without audio must still mix cleanly. amix keeps
    // the base length.
    QString fc;
    if (mergedHasAudio && !muteVideoAudio)
        fc = QStringLiteral("[0:a]aformat=sample_rates=48000:channel_layouts=stereo[v0];");
    else
        fc = QStringLiteral("anullsrc=r=48000:cl=stereo:d=%1[v0];").arg(totalDuration, 0, 'f', 3);

    for (int i = 0; i < clips.size(); ++i) {
        const AudioSpec &clip = clips.at(i);
        fc += QStringLiteral("[%1:a]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS,"
                             "aformat=sample_rates=48000:channel_layouts=stereo,"
                             "adelay=%4|%4[a%5];")
                  .arg(i + 1)
                  .arg(clip.startSec, 0, 'f', 3)
                  .arg(clip.endSec, 0, 'f', 3)
                  .arg(qRound(clip.positionSec * 1000.0))
                  .arg(i + 1);
    }

    fc += QStringLiteral("[v0]");
    for (int i = 0; i < clips.size(); ++i)
        fc += QStringLiteral("[a%1]").arg(i + 1);
    fc += QStringLiteral("amix=inputs=%1:duration=first:normalize=0[aout]").arg(clips.size() + 1);

    args << "-filter_complex" << fc;
    args << "-map" << "0:v" << "-map" << "[aout]";
    args << "-c:v" << "copy" << "-c:a" << "aac"
         << "-movflags" << "+faststart"
         << "-t" << QString::number(totalDuration, 'f', 3)
         << dst;
    return args;
}

}  // namespace ffmpeg
