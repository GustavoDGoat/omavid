#pragma once

#include <QAbstractListModel>
#include <QImage>
#include <QUrl>
#include <QVector>

// One video in the project: its source, probed info, the user's trim, its
// optional replacement audio, and the cached full-length filmstrip (so switching
// clips never regenerates thumbs).
struct Clip {
    QUrl url;
    QString path;
    QString name;
    double duration = 0.0;
    int width = 0;
    int height = 0;
    double startSec = 0.0;
    double endSec = 0.0;
    bool ok = false;
    QString error;
    QVector<QImage> thumbs;
    bool thumbsComplete = false;

    // Optional replacement audio: when set, the clip's own audio is muted and
    // this trimmed audio plays instead.
    QString audioPath;
    QString audioName;
    double audioDuration = 0.0;
    double audioStartSec = 0.0;
    double audioEndSec = 0.0;

    bool trimmed() const {
        return ok && (startSec > 0.0 || endSec < duration);
    }
    bool hasAudio() const {
        return !audioPath.isEmpty();
    }
};

// The playlist model the sidebar renders. Mutations go through the Backend, which
// keeps the current clip, filmstrip and thumbnails in sync.
class ClipListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        UrlRole = Qt::UserRole + 1,
        NameRole,
        DurationRole,
        StartRole,
        EndRole,
        OkRole,
        ErrorRole,
        TrimmedRole,
        HasAudioRole,
        AudioNameRole,
        AudioStartRole,
        AudioEndRole,
        AudioDurationRole,
    };

    explicit ClipListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_clips.size(); }
    const Clip &at(int index) const { return m_clips.at(index); }
    Clip &at(int index) { return m_clips[index]; }

    // Returns the row of the inserted clip.
    int append(const Clip &clip);
    void removeAt(int index);
    void move(int from, int to);
    // Updates a clip's trim in place; returns true if the value changed.
    bool setTrim(int index, double start, double end);
    // Updates a clip's replacement audio in place; returns true if changed.
    bool setAudio(int index, const QString &path, const QString &name, double duration,
                  double start, double end);
    bool clearAudio(int index);

private:
    QVector<Clip> m_clips;
};

// One audio clip on the merge audio track: its source, its trim within the file,
// and where it starts in the merged output.
struct AudioClip {
    QString path;
    QString name;
    double duration = 0.0;
    double startSec = 0.0;
    double endSec = 0.0;
    double positionSec = 0.0;

    bool trimmed() const { return startSec > 0.0 || endSec < duration; }
};

// The audio-track model rendered as the draggable audio timeline.
class AudioTrackModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        DurationRole,
        StartRole,
        EndRole,
        PositionRole,
        TrimmedRole,
    };

    explicit AudioTrackModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_clips.size(); }
    const AudioClip &at(int index) const { return m_clips.at(index); }
    AudioClip &at(int index) { return m_clips[index]; }

    int append(const AudioClip &clip);
    void removeAt(int index);
    bool setTrim(int index, double start, double end);
    bool setPosition(int index, double position);

private:
    QVector<AudioClip> m_clips;
};
