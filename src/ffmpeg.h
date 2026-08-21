#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>

// Thin wrappers around the ffmpeg/ffprobe command-line tools.
namespace ffmpeg {

struct VideoInfo {
    QString path;
    double duration = 0.0;  // seconds
    int width = 0;
    int height = 0;
    bool ok = false;
    QString error;
};

// A replacement/overlay audio clip. `startSec`/`endSec` are the trim within the
// audio file; `positionSec` is where the trimmed audio starts in the merged
// output (ignored for per-clip replacement, which always starts at the clip).
struct AudioSpec {
    QString path;
    double startSec = 0.0;
    double endSec = 0.0;
    double positionSec = 0.0;

    bool valid() const { return !path.isEmpty() && endSec > startSec; }
};

// Probe a file for a usable video stream and duration (runs ffprobe).
VideoInfo probe(const QString &path);

// Probe a file for a usable audio stream and duration (runs ffprobe).
VideoInfo audioProbe(const QString &path);

// Whether the file carries any audio stream (runs ffprobe).
bool hasAudioStream(const QString &path);

// Grab a single frame at `time` seconds, scaled to `height` px.
// Returns a null QImage on failure. If `cancel` is set and flips to true while
// the ffmpeg child is running, the child is killed and a null QImage returned.
QImage thumbnail(const QString &path, double time, int height = 90,
                 const std::atomic<bool> *cancel = nullptr);

// Build the ffmpeg argument list that writes [start, end] of src to dst.
// Cuts are frame-accurate and re-encoded with libx264/aac. A non-zero
// scaleHeight downscales so the shorter side becomes scaleHeight (1080p of a
// portrait video is 1080 wide), always preserving the aspect ratio.
// When `audio` is valid, the video's own audio is dropped and the trimmed
// `audio` is used instead (padded with silence to match the clip length).
QStringList trimArgs(const QString &src, const QString &dst, double start, double end,
                     int scaleHeight = 0, const AudioSpec &audio = {});

// Like trimArgs, but also normalizes the output (48 kHz stereo audio, yuv420p)
// so every intermediate in a merge is byte-compatible for a lossless concat.
QStringList mergeClipArgs(const QString &src, const QString &dst, double start, double end,
                          int scaleHeight = 0, const AudioSpec &audio = {});

// Build the concat-demuxer argument list that joins the intermediates named in
// `listPath` (one "file '...'" per line) into dst with stream copy.
QStringList concatArgs(const QString &listPath, const QString &dst);

// Build the argument list that mixes the `clips` (trimmed + positioned) over
// the audio of `mergedSrc`, writing to dst. `mergedHasAudio` tells whether the
// merged video actually carries an audio stream — when it doesn't (or when
// `muteVideoAudio` is set) a silent base is synthesized instead, so sources
// without audio still mix cleanly. `totalDuration` seconds bounds the output.
QStringList mixArgs(const QString &mergedSrc, const QString &dst,
                    const QList<AudioSpec> &clips, double totalDuration,
                    bool muteVideoAudio, bool mergedHasAudio);

// Locate a tool on PATH; returns empty string if missing.
QString toolPath(const QString &tool);

}  // namespace ffmpeg
