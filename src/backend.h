#pragma once

#include <QFileSystemWatcher>
#include <QImage>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVector>

#include <memory>

#include "clip.h"
#include "ffmpeg.h"

class ThumbProvider;
class FilePicker;
class ThumbWorker;

// The bridge between QML and the ffmpeg/ffprobe layer. Holds the playlist of
// clips and the audio track, and drives thumbnail generation, single-clip
// export and the merge.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY infoChanged)
    Q_PROPERTY(double duration READ duration NOTIFY infoChanged)
    Q_PROPERTY(int thumbCount READ thumbCount NOTIFY thumbsChanged)
    Q_PROPERTY(int thumbReadyCount READ thumbReadyCount NOTIFY thumbsChanged)
    Q_PROPERTY(int thumbRevision READ thumbRevision NOTIFY thumbsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeAccentChanged)
    Q_PROPERTY(QString themeAccentForeground READ themeAccentForeground NOTIFY themeAccentChanged)
    Q_PROPERTY(ClipListModel *clips READ clips CONSTANT)
    Q_PROPERTY(int clipCount READ clipCount NOTIFY clipsChanged)
    Q_PROPERTY(int clipRevision READ clipRevision NOTIFY clipsChanged)
    Q_PROPERTY(bool anyClipTrimmed READ anyClipTrimmed NOTIFY clipsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(double clipStartSec READ clipStartSec NOTIFY currentIndexChanged)
    Q_PROPERTY(double clipEndSec READ clipEndSec NOTIFY currentIndexChanged)
    Q_PROPERTY(bool clipHasAudio READ clipHasAudio NOTIFY currentIndexChanged)
    Q_PROPERTY(double clipAudioStart READ clipAudioStart NOTIFY currentIndexChanged)
    Q_PROPERTY(double clipAudioEnd READ clipAudioEnd NOTIFY currentIndexChanged)
    Q_PROPERTY(double clipAudioDuration READ clipAudioDuration NOTIFY currentIndexChanged)
    Q_PROPERTY(QString clipAudioName READ clipAudioName NOTIFY currentIndexChanged)
    Q_PROPERTY(AudioTrackModel *audioTrack READ audioTrack CONSTANT)
    Q_PROPERTY(int audioTrackCount READ audioTrackCount NOTIFY clipsChanged)
    Q_PROPERTY(bool muteVideoAudio READ muteVideoAudio NOTIFY clipsChanged)
    Q_PROPERTY(double mergeDuration READ mergeDuration NOTIFY clipsChanged)
    Q_PROPERTY(bool anyAudioWork READ anyAudioWork NOTIFY clipsChanged)

public:
    explicit Backend(ThumbProvider *provider, QObject *parent = nullptr);
    explicit Backend(ThumbProvider *provider, FilePicker *filePicker,
                     QObject *parent = nullptr);
    ~Backend() override;

    QUrl source() const;
    double duration() const;
    int thumbCount() const { return m_thumbCount; }
    int thumbReadyCount() const { return m_thumbReadyCount; }
    int thumbRevision() const { return m_thumbRevision; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeAccentForeground() const;
    ClipListModel *clips() const { return m_clips; }
    int clipCount() const { return m_clips->count(); }
    int clipRevision() const { return m_clipRevision; }
    bool anyClipTrimmed() const;
    int currentIndex() const { return m_currentIndex; }
    double clipStartSec() const;
    double clipEndSec() const;
    bool clipHasAudio() const;
    double clipAudioStart() const;
    double clipAudioEnd() const;
    double clipAudioDuration() const;
    QString clipAudioName() const;
    AudioTrackModel *audioTrack() const { return m_audioTrack; }
    int audioTrackCount() const { return m_audioTrack->count(); }
    bool muteVideoAudio() const { return m_muteVideoAudio; }
    double mergeDuration() const;
    bool anyAudioWork() const;

    // The accent from an omarchy colors.toml, or the fallback when the file is
    // missing or holds no usable accent — which is what keeps omavid working on
    // distros without omarchy themes.
    static QString accentFromColorsFile(const QString &path, const QString &fallback);
    // "black" or "white", whichever stays legible on the given color.
    static QString foregroundFor(const QString &color);

    // Load a single video (probes it, then kicks off thumbnail generation).
    // Returns false if the probe failed.
    Q_INVOKABLE bool load(const QUrl &url);

    // Append a batch of clips (from the file picker) and select the first that
    // probed successfully.
    void addClips(const QList<QUrl> &urls);

    Q_INVOKABLE void selectClip(int index);
    Q_INVOKABLE void removeClip(int index);
    Q_INVOKABLE void moveClip(int from, int to);
    Q_INVOKABLE void setClipTrim(int index, double start, double end);

    // Replacement audio: attach to a clip (mutes its own audio) or detach.
    Q_INVOKABLE void attachAudioDialog(int clipIndex);
    Q_INVOKABLE void detachAudio(int clipIndex);
    Q_INVOKABLE void setClipAudioTrim(int clipIndex, double start, double end);

    // Merge audio track: add/remove clips and move them on the timeline.
    Q_INVOKABLE void addAudioDialog();
    Q_INVOKABLE void removeAudio(int index);
    Q_INVOKABLE void setAudioTrim(int index, double start, double end);
    Q_INVOKABLE void setAudioPosition(int index, double position);
    Q_INVOKABLE void setMuteVideoAudio(bool mute);

    // Open native desktop file dialogs.
    Q_INVOKABLE void openVideoDialog();
    Q_INVOKABLE void exportDialog(double start, double end);
    Q_INVOKABLE void exportMergeDialog();

    // Suggested "<name>_trimmed.mp4" target next to the current source.
    Q_INVOKABLE QUrl suggestedExportUrl() const;
    // Suggested "<first-clip>_merged.mp4" target next to the first clip.
    Q_INVOKABLE QUrl suggestedMergeUrl() const;

    // Write [start, end] (seconds) of the current clip to dst. A non-zero
    // scaleHeight downscales the shorter side to that size.
    Q_INVOKABLE void exportClip(const QUrl &dst, double start, double end,
                                int scaleHeight = 0);

    // Re-encode every clip (with its own trim) and concatenate them into dst,
    // then mix the audio track over it.
    Q_INVOKABLE void exportMerge(const QUrl &dst, int scaleHeight = 0);

    // The downscale heights worth offering for a source: only ones strictly
    // below the source's shorter side, so exports never upscale.
    static QList<int> exportHeights(int width, int height);
    // Merge never upscales any clip, so it's capped by the smallest short side.
    Q_INVOKABLE QList<int> mergeExportHeights() const;

    // Regenerate the filmstrip for [start, end] (seconds) — used by zoom.
    // The full-length strip is cached, so zooming back out restores instantly.
    Q_INVOKABLE void requestThumbs(double start, double end);

signals:
    void infoChanged();
    void thumbsChanged();
    void busyChanged();
    void statusChanged();
    void themeAccentChanged();
    void clipsChanged();
    void currentIndexChanged();
    void exportDone(const QString &path);
    void mergeDone(const QString &path);
    void exportFailed(const QString &message);
    void loadError(const QString &message);

private:
    struct MergeContext;

    void setBusy(bool busy);
    void setStatus(const QString &status);
    void failExport(const QString &tmpPath, const QString &message);
    void startThumbs();
    void stopThumbs();
    void clearFilmstrip();
    void revealNextThumb();
    void wireFilePicker();
    void loadThemeAccent();
    void watchTheme();

    const Clip *currentClip() const;
    Clip *currentClip();
    bool hasCurrentClip() const;
    int appendClip(const QUrl &url, const ffmpeg::VideoInfo &info);
    void bumpClipRevision();
    QString resolveExportPath(const QString &selectedPath);
    ffmpeg::AudioSpec audioSpecFor(const Clip &clip) const;
    void handleAudioSelected(const QUrl &url);

    void runNextMergeClip(const std::shared_ptr<MergeContext> &ctx);
    void runMergeConcat(const std::shared_ptr<MergeContext> &ctx);
    void runMergeMix(const std::shared_ptr<MergeContext> &ctx);
    void finishMerge(const std::shared_ptr<MergeContext> &ctx);
    void failMerge(const std::shared_ptr<MergeContext> &ctx, const QString &message);
    static void cleanupMergeTemp(const std::shared_ptr<MergeContext> &ctx);

    ThumbProvider *m_provider;
    FilePicker *m_filePicker;
    ClipListModel *m_clips;
    AudioTrackModel *m_audioTrack;
    int m_currentIndex = -1;
    int m_clipRevision = 0;
    int m_pendingAudioClipIndex = -1;
    bool m_muteVideoAudio = false;
    ThumbWorker *m_thumbWorker = nullptr;
    double m_thumbStart = 0.0;
    double m_thumbLen = 0.0;
    int m_thumbCount = 0;
    int m_thumbAvailableCount = 0;
    int m_thumbReadyCount = 0;
    int m_thumbRevision = 0;
    bool m_thumbWorkerDone = false;
    bool m_busy = false;
    QString m_status;
    QString m_themeAccent;
    QTimer m_thumbRevealTimer;
    QFileSystemWatcher m_themeWatcher;
};
