#include "backend.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

#include <cstdio>
#include <memory>

#include "filepicker.h"
#include "portalfilepicker.h"
#include "thumbprovider.h"
#include "thumbworker.h"

namespace {
constexpr int kThumbCount = 12;
constexpr int kThumbRevealMs = 70;
const QString kDefaultAccent = QStringLiteral("#FFD60A");

QString omarchyCurrentDir() {
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
}

QString omarchyColorsPath() {
    return omarchyCurrentDir() + QStringLiteral("/theme/colors.toml");
}

QString mp4PathFor(const QString &path) {
    const QFileInfo file(path);
    if (file.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0)
        return path;

    const QString baseName = file.completeBaseName().isEmpty()
        ? file.fileName()
        : file.completeBaseName();
    return file.dir().filePath(baseName + QStringLiteral(".mp4"));
}

bool replaceWithTemp(const QString &tmpPath, const QString &outPath) {
    const QByteArray tmpName = QFile::encodeName(tmpPath);
    const QByteArray outName = QFile::encodeName(outPath);
    return std::rename(tmpName.constData(), outName.constData()) == 0;
}

QString concatFileLine(const QString &path) {
    QString escaped = path;
    escaped.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QStringLiteral("file '%1'\n").arg(escaped);
}
}

// State for a merge run: the per-clip encode jobs, then one concat pass.
struct Backend::MergeContext {
    QString ffmpegBin;
    QString outPath;
    QString tmpDir;
    QString listPath;
    QString tmpPath;
    QStringList dstPaths;
    QList<QStringList> argLists;
    int next = 0;
    std::shared_ptr<bool> completed = std::make_shared<bool>(false);
};

Backend::Backend(ThumbProvider *provider, QObject *parent)
    : Backend(provider, new PortalFilePicker(), parent) {}

Backend::Backend(ThumbProvider *provider, FilePicker *filePicker, QObject *parent)
    : QObject(parent), m_provider(provider), m_filePicker(filePicker),
      m_clips(new ClipListModel(this)), m_themeAccent(kDefaultAccent) {
    if (!m_filePicker->parent())
        m_filePicker->setParent(this);
    wireFilePicker();
    m_thumbRevealTimer.setInterval(kThumbRevealMs);
    connect(&m_thumbRevealTimer, &QTimer::timeout, this, &Backend::revealNextThumb);

    // Follow omarchy theme switches live. The theme lives behind a symlink that
    // gets swapped, so the reload also re-arms the watch paths every time.
    const auto themeChanged = [this] {
        watchTheme();
        loadThemeAccent();
    };
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, themeChanged);
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, themeChanged);
    watchTheme();
    loadThemeAccent();
}

Backend::~Backend() {
    stopThumbs();
}

void Backend::wireFilePicker() {
    connect(m_filePicker, &FilePicker::openSelected, this, &Backend::addClips);
    connect(m_filePicker, &FilePicker::exportSelected, this, &Backend::exportClip);
    connect(m_filePicker, &FilePicker::exportMergedSelected, this, &Backend::exportMerge);
    connect(m_filePicker, &FilePicker::failed, this, &Backend::loadError);
}

void Backend::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

QString Backend::accentFromColorsFile(const QString &path, const QString &fallback) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fallback;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0 || line.left(equals).trimmed() != QStringLiteral("accent"))
            continue;

        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2
                && ((value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
                    || (value.front() == QLatin1Char('\'') && value.back() == QLatin1Char('\''))))
            value = value.mid(1, value.size() - 2);

        return QColor::fromString(value).isValid() ? value : fallback;
    }
    return fallback;
}

QString Backend::foregroundFor(const QString &color) {
    const QColor parsed = QColor::fromString(color);
    if (!parsed.isValid())
        return QStringLiteral("black");
    const double luminance = 0.299 * parsed.redF()
        + 0.587 * parsed.greenF() + 0.114 * parsed.blueF();
    return luminance < 0.5 ? QStringLiteral("white") : QStringLiteral("black");
}

QString Backend::themeAccentForeground() const {
    return foregroundFor(m_themeAccent);
}

void Backend::loadThemeAccent() {
    const QString accent = accentFromColorsFile(omarchyColorsPath(), kDefaultAccent);
    if (accent == m_themeAccent)
        return;
    m_themeAccent = accent;
    emit themeAccentChanged();
}

void Backend::watchTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = omarchyCurrentDir();
    const QString themeDir = currentDir + QStringLiteral("/theme");
    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFileInfo::exists(omarchyColorsPath()))
        m_themeWatcher.addPath(omarchyColorsPath());
}

const Clip *Backend::currentClip() const {
    return (m_currentIndex >= 0 && m_currentIndex < m_clips->count())
        ? &m_clips->at(m_currentIndex) : nullptr;
}

Clip *Backend::currentClip() {
    return (m_currentIndex >= 0 && m_currentIndex < m_clips->count())
        ? &m_clips->at(m_currentIndex) : nullptr;
}

bool Backend::hasCurrentClip() const {
    return currentClip() != nullptr;
}

QUrl Backend::source() const {
    const Clip *clip = currentClip();
    return clip ? clip->url : QUrl();
}

double Backend::duration() const {
    const Clip *clip = currentClip();
    return clip ? clip->duration : 0.0;
}

double Backend::clipStartSec() const {
    const Clip *clip = currentClip();
    return clip ? clip->startSec : 0.0;
}

double Backend::clipEndSec() const {
    const Clip *clip = currentClip();
    return clip ? clip->endSec : 0.0;
}

bool Backend::anyClipTrimmed() const {
    for (int i = 0; i < m_clips->count(); ++i) {
        if (m_clips->at(i).trimmed())
            return true;
    }
    return false;
}

void Backend::bumpClipRevision() {
    ++m_clipRevision;
    emit clipsChanged();
}

bool Backend::load(const QUrl &url) {
    const ffmpeg::VideoInfo info = ffmpeg::probe(url.toLocalFile());
    if (!info.ok) {
        emit loadError(info.error);
        return false;
    }
    const int index = appendClip(url, info);
    selectClip(index);
    return true;
}

void Backend::addClips(const QList<QUrl> &urls) {
    int firstAdded = -1;
    for (const QUrl &url : urls) {
        const ffmpeg::VideoInfo info = ffmpeg::probe(url.toLocalFile());
        if (!info.ok) {
            emit loadError(info.error);
            continue;
        }
        const int index = appendClip(url, info);
        if (firstAdded < 0)
            firstAdded = index;
    }
    if (firstAdded >= 0)
        selectClip(firstAdded);
}

int Backend::appendClip(const QUrl &url, const ffmpeg::VideoInfo &info) {
    Clip clip;
    clip.url = url;
    clip.path = info.path;
    clip.name = QFileInfo(info.path).fileName();
    clip.duration = info.duration;
    clip.width = info.width;
    clip.height = info.height;
    clip.startSec = 0.0;
    clip.endSec = info.duration;
    clip.ok = true;
    clip.thumbs = QVector<QImage>(kThumbCount);
    const int index = m_clips->append(clip);
    bumpClipRevision();
    return index;
}

void Backend::selectClip(int index) {
    if (index < 0 || index >= m_clips->count() || index == m_currentIndex)
        return;
    m_currentIndex = index;
    clearFilmstrip();

    const Clip *clip = currentClip();
    m_thumbStart = 0.0;
    m_thumbLen = clip->duration;

    // Restore the cached full-length strip instantly if we have it.
    if (clip->thumbsComplete) {
        m_provider->setImages(clip->thumbs);
        m_thumbAvailableCount = kThumbCount;
        m_thumbReadyCount = kThumbCount;
        m_thumbWorkerDone = true;
        emit thumbsChanged();
    } else {
        setStatus(QStringLiteral("Loading..."));
        startThumbs();
    }

    emit currentIndexChanged();
    emit infoChanged();
}

void Backend::removeClip(int index) {
    if (index < 0 || index >= m_clips->count())
        return;
    const bool removedCurrent = index == m_currentIndex;
    m_clips->removeAt(index);
    bumpClipRevision();

    if (m_clips->count() == 0) {
        m_currentIndex = -1;
        clearFilmstrip();
        emit currentIndexChanged();
        emit infoChanged();
    } else if (removedCurrent) {
        m_currentIndex = -1;  // force re-selection through selectClip
        selectClip(qMin(index, m_clips->count() - 1));
    } else if (index < m_currentIndex) {
        --m_currentIndex;
        emit currentIndexChanged();
    }
}

void Backend::moveClip(int from, int to) {
    if (from < 0 || from >= m_clips->count() || to < 0 || to >= m_clips->count() || from == to)
        return;
    m_clips->move(from, to);
    if (m_currentIndex == from) {
        m_currentIndex = to;
        emit currentIndexChanged();
    } else if (from < m_currentIndex && to >= m_currentIndex) {
        --m_currentIndex;
        emit currentIndexChanged();
    } else if (from > m_currentIndex && to <= m_currentIndex) {
        ++m_currentIndex;
        emit currentIndexChanged();
    }
    bumpClipRevision();
}

void Backend::setClipTrim(int index, double start, double end) {
    if (index < 0 || index >= m_clips->count())
        return;
    const Clip &clip = m_clips->at(index);
    start = qBound(0.0, start, clip.duration);
    end = qBound(start, end, clip.duration);
    if (m_clips->setTrim(index, start, end))
        bumpClipRevision();
}

void Backend::openVideoDialog() {
    m_filePicker->openVideo();
}

void Backend::exportDialog(double start, double end) {
    const Clip *clip = currentClip();
    if (!clip)
        return;

    m_filePicker->exportVideo(suggestedExportUrl(), start, end,
                              exportHeights(clip->width, clip->height));
}

void Backend::exportMergeDialog() {
    if (m_clips->count() < 2 || m_busy)
        return;

    m_filePicker->exportMerged(suggestedMergeUrl(), mergeExportHeights());
}

QList<int> Backend::exportHeights(int width, int height) {
    const int shortSide = qMin(width, height);
    QList<int> heights;
    for (const int candidate : {1080, 720}) {
        if (shortSide > candidate)
            heights << candidate;
    }
    return heights;
}

QList<int> Backend::mergeExportHeights() const {
    int minShort = -1;
    for (int i = 0; i < m_clips->count(); ++i) {
        const Clip &clip = m_clips->at(i);
        const int shortSide = qMin(clip.width, clip.height);
        if (shortSide > 0 && (minShort < 0 || shortSide < minShort))
            minShort = shortSide;
    }
    if (minShort < 0)
        return {};
    return exportHeights(minShort, minShort);
}

QUrl Backend::suggestedExportUrl() const {
    const Clip *clip = currentClip();
    if (!clip)
        return {};
    const QFileInfo src(clip->path);
    const QString target = src.dir().filePath(src.completeBaseName() + "_trimmed.mp4");
    return QUrl::fromLocalFile(target);
}

QUrl Backend::suggestedMergeUrl() const {
    if (m_clips->count() == 0)
        return {};
    const QFileInfo src(m_clips->at(0).path);
    const QString target = src.dir().filePath(src.completeBaseName() + "_merged.mp4");
    return QUrl::fromLocalFile(target);
}

void Backend::startThumbs() {
    const Clip *clip = currentClip();
    if (!clip)
        return;

    auto *worker = new ThumbWorker(clip->path, m_thumbStart, m_thumbLen, kThumbCount);
    m_thumbWorker = worker;
    // Pair the pointer check with the revision: a recycled worker address could
    // otherwise let a stale queued callback write into the new filmstrip.
    const int revision = m_thumbRevision;

    connect(worker, &ThumbWorker::thumbReady, this, [this, worker, revision](int index, const QImage &image) {
        if (worker != m_thumbWorker || revision != m_thumbRevision)
            return;
        m_provider->setImage(index, image);
        // Thumbs arrive in order, so the strip is fully cached at the last one.
        if (m_thumbStart <= 0.0 && m_thumbLen >= currentClip()->duration) {
            Clip *clip = currentClip();
            clip->thumbs[index] = image;
            if (index == kThumbCount - 1)
                clip->thumbsComplete = true;
        }
        m_thumbAvailableCount = qMax(m_thumbAvailableCount, index + 1);
        if (m_thumbReadyCount == 0)
            revealNextThumb();
        if (!m_thumbRevealTimer.isActive())
            m_thumbRevealTimer.start();
    });
    connect(worker, &ThumbWorker::finished, this, [this, worker, revision] {
        if (worker == m_thumbWorker && revision == m_thumbRevision) {
            m_thumbWorker = nullptr;
            m_thumbWorkerDone = true;
            if (m_thumbReadyCount >= m_thumbCount)
                setStatus(QString());
            else if (!m_thumbRevealTimer.isActive())
                m_thumbRevealTimer.start();
        }
        worker->deleteLater();
    });
    worker->start();
}

void Backend::revealNextThumb() {
    if (m_thumbReadyCount < m_thumbAvailableCount) {
        ++m_thumbReadyCount;
        emit thumbsChanged();
    }

    if (m_thumbReadyCount < m_thumbAvailableCount)
        return;

    m_thumbRevealTimer.stop();
    if (m_thumbWorkerDone && m_thumbReadyCount >= m_thumbCount)
        setStatus(QString());
}

void Backend::stopThumbs() {
    m_thumbRevealTimer.stop();
    if (!m_thumbWorker)
        return;

    ThumbWorker *worker = m_thumbWorker;
    m_thumbWorker = nullptr;
    worker->disconnect(this);
    worker->requestStop();
    worker->wait();
    delete worker;
}

void Backend::clearFilmstrip() {
    stopThumbs();
    ++m_thumbRevision;
    m_thumbCount = kThumbCount;
    m_provider->setImages(QVector<QImage>(kThumbCount));
    m_thumbAvailableCount = 0;
    m_thumbReadyCount = 0;
    m_thumbWorkerDone = false;
    emit thumbsChanged();
}

void Backend::requestThumbs(double start, double end) {
    const Clip *clip = currentClip();
    if (!clip)
        return;
    start = qBound(0.0, start, clip->duration);
    end = qBound(start, end, clip->duration);
    if (end - start <= 0.0 || (start == m_thumbStart && end - start == m_thumbLen))
        return;

    stopThumbs();
    m_thumbStart = start;
    m_thumbLen = end - start;
    ++m_thumbRevision;

    // Zooming back out: restore the cached full-length strip instantly.
    if (start <= 0.0 && end >= clip->duration && clip->thumbsComplete) {
        m_provider->setImages(clip->thumbs);
        m_thumbAvailableCount = kThumbCount;
        m_thumbReadyCount = kThumbCount;
        m_thumbWorkerDone = true;
        emit thumbsChanged();
        return;
    }

    m_thumbAvailableCount = 0;
    m_thumbReadyCount = 0;
    m_thumbWorkerDone = false;
    m_provider->setImages(QVector<QImage>(kThumbCount));
    emit thumbsChanged();
    startThumbs();
}

QString Backend::resolveExportPath(const QString &selectedPath) {
    // Forcing the .mp4 suffix can redirect the write to a file the save
    // dialog never asked the user about overwriting — refuse rather than
    // silently replace it.
    const QString outPath = mp4PathFor(selectedPath);
    if (outPath != selectedPath && QFileInfo::exists(outPath)) {
        emit exportFailed(QStringLiteral("%1 already exists.")
                              .arg(QFileInfo(outPath).fileName()));
        return {};
    }

    if (ffmpeg::toolPath("ffmpeg").isEmpty()) {
        emit exportFailed("`ffmpeg` was not found on your PATH.");
        return {};
    }
    return outPath;
}

void Backend::exportClip(const QUrl &dst, double start, double end, int scaleHeight) {
    const Clip *clip = currentClip();
    if (!clip || m_busy)
        return;

    if (end - start <= 0.0) {
        emit exportFailed("The selected clip has no length.");
        return;
    }

    const QString outPath = resolveExportPath(dst.toLocalFile());
    if (outPath.isEmpty())
        return;

    setBusy(true);
    setStatus(QStringLiteral("Exporting 0%"));

    // Encode to a sibling temp file and atomically replace the target only after
    // success, so failed/cancelled exports preserve any existing file.
    const QString tmpPath = outPath + QStringLiteral(".omacut-part.mp4");
    QFile::remove(tmpPath);
    const QStringList args = ffmpeg::trimArgs(clip->path, tmpPath, start, end, scaleHeight);
    const QString ffmpegBin = ffmpeg::toolPath("ffmpeg");

    auto *proc = new QProcess(this);
    auto completed = std::make_shared<bool>(false);

    // ffmpeg -progress writes key=value blocks to stdout as it encodes;
    // out_time_us against the clip length gives the percentage.
    const double clipLen = end - start;
    auto progressBuf = std::make_shared<QByteArray>();
    connect(proc, &QProcess::readyReadStandardOutput, this,
            [this, proc, progressBuf, clipLen, completed] {
                progressBuf->append(proc->readAllStandardOutput());
                int newline;
                while ((newline = progressBuf->indexOf('\n')) >= 0) {
                    const QByteArray line = progressBuf->left(newline).trimmed();
                    progressBuf->remove(0, newline + 1);
                    if (*completed || !line.startsWith("out_time_us="))
                        continue;
                    bool ok = false;
                    const double outSecs = line.mid(line.indexOf('=') + 1).toLongLong(&ok) / 1e6;
                    if (!ok)
                        continue;
                    const int percent = qBound(0, qRound(outSecs / clipLen * 100.0), 100);
                    setStatus(QStringLiteral("Exporting %1%").arg(percent));
                }
            });

    connect(proc, &QProcess::finished, this,
            [this, proc, outPath, tmpPath, completed](int code, QProcess::ExitStatus exitStatus) {
                if (*completed)
                    return;
                *completed = true;
                const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                if (exitStatus != QProcess::NormalExit || code != 0) {
                    failExport(tmpPath, err.isEmpty() ? QStringLiteral("ffmpeg trim failed.") : err);
                    return;
                }
                if (!replaceWithTemp(tmpPath, outPath)) {
                    failExport(tmpPath, QStringLiteral("Could not write the exported file."));
                    return;
                }
                setBusy(false);
                setStatus(QString());
                emit exportDone(outPath);
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, tmpPath, completed](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || *completed)
                    return;
                *completed = true;
                const QString err = proc->errorString();
                proc->deleteLater();
                failExport(tmpPath, err.isEmpty() ? QStringLiteral("Could not start ffmpeg.") : err);
            });
    proc->start(ffmpegBin, args);
}

void Backend::failExport(const QString &tmpPath, const QString &message) {
    setBusy(false);
    setStatus(QString());
    QFile::remove(tmpPath);
    emit exportFailed(message);
}

void Backend::exportMerge(const QUrl &dst, int scaleHeight) {
    if (m_clips->count() == 0 || m_busy)
        return;

    const QString outPath = resolveExportPath(dst.toLocalFile());
    if (outPath.isEmpty())
        return;

    setBusy(true);
    setStatus(QStringLiteral("Preparing..."));

    auto ctx = std::make_shared<MergeContext>();
    ctx->ffmpegBin = ffmpeg::toolPath("ffmpeg");
    ctx->outPath = outPath;
    ctx->tmpDir = outPath + QStringLiteral(".omacut-merge");
    ctx->listPath = ctx->tmpDir + QStringLiteral("/list.txt");
    ctx->tmpPath = outPath + QStringLiteral(".omacut-part.mp4");
    QDir().mkpath(ctx->tmpDir);

    for (int i = 0; i < m_clips->count(); ++i) {
        const Clip &clip = m_clips->at(i);
        const QString dstPath = ctx->tmpDir + QStringLiteral("/clip_%1.mp4").arg(i);
        ctx->dstPaths << dstPath;
        ctx->argLists << ffmpeg::mergeClipArgs(clip.path, dstPath, clip.startSec, clip.endSec,
                                               scaleHeight);
    }

    runNextMergeClip(ctx);
}

void Backend::runNextMergeClip(const std::shared_ptr<MergeContext> &ctx) {
    if (*ctx->completed)
        return;
    if (ctx->next >= ctx->argLists.size()) {
        runMergeConcat(ctx);
        return;
    }

    const int i = ctx->next++;
    setStatus(QStringLiteral("Exporting clip %1 of %2").arg(i + 1).arg(ctx->argLists.size()));

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::readyReadStandardOutput, proc, [proc] {
        proc->readAllStandardOutput();  // drain -progress so ffmpeg never blocks
    });
    connect(proc, &QProcess::finished, this,
            [this, ctx, proc](int code, QProcess::ExitStatus exitStatus) {
                if (*ctx->completed) {
                    proc->deleteLater();
                    return;
                }
                const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                if (exitStatus != QProcess::NormalExit || code != 0) {
                    *ctx->completed = true;
                    failMerge(ctx, err.isEmpty()
                                      ? QStringLiteral("ffmpeg failed while encoding a clip.") : err);
                    return;
                }
                runNextMergeClip(ctx);
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, ctx, proc](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || *ctx->completed)
                    return;
                *ctx->completed = true;
                const QString err = proc->errorString();
                proc->deleteLater();
                failMerge(ctx, err.isEmpty() ? QStringLiteral("Could not start ffmpeg.") : err);
            });
    proc->start(ctx->ffmpegBin, ctx->argLists.at(i));
}

void Backend::runMergeConcat(const std::shared_ptr<MergeContext> &ctx) {
    QFile list(ctx->listPath);
    if (!list.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *ctx->completed = true;
        failMerge(ctx, QStringLiteral("Could not prepare the merge list."));
        return;
    }
    for (const QString &dstPath : ctx->dstPaths)
        list.write(concatFileLine(dstPath).toUtf8());
    list.close();

    setStatus(QStringLiteral("Merging..."));
    const QStringList concat = ffmpeg::concatArgs(ctx->listPath, ctx->tmpPath);

    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this,
            [this, ctx, proc](int code, QProcess::ExitStatus exitStatus) {
                if (*ctx->completed) {
                    proc->deleteLater();
                    return;
                }
                const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                if (exitStatus != QProcess::NormalExit || code != 0) {
                    *ctx->completed = true;
                    failMerge(ctx, err.isEmpty()
                                      ? QStringLiteral("ffmpeg failed while merging.") : err);
                    return;
                }
                if (!replaceWithTemp(ctx->tmpPath, ctx->outPath)) {
                    *ctx->completed = true;
                    failMerge(ctx, QStringLiteral("Could not write the merged file."));
                    return;
                }
                *ctx->completed = true;
                cleanupMergeTemp(ctx);
                setBusy(false);
                setStatus(QString());
                emit mergeDone(ctx->outPath);
            });
    connect(proc, &QProcess::errorOccurred, this,
            [this, ctx, proc](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || *ctx->completed)
                    return;
                *ctx->completed = true;
                const QString err = proc->errorString();
                proc->deleteLater();
                failMerge(ctx, err.isEmpty() ? QStringLiteral("Could not start ffmpeg.") : err);
            });
    proc->start(ctx->ffmpegBin, concat);
}

void Backend::failMerge(const std::shared_ptr<MergeContext> &ctx, const QString &message) {
    cleanupMergeTemp(ctx);
    setBusy(false);
    setStatus(QString());
    emit exportFailed(message);
}

void Backend::cleanupMergeTemp(const std::shared_ptr<MergeContext> &ctx) {
    QFile::remove(ctx->tmpPath);
    QDir(ctx->tmpDir).removeRecursively();
}
