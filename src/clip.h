#pragma once

#include <QAbstractListModel>
#include <QImage>
#include <QUrl>
#include <QVector>

// One video in the project: its source, probed info, the user's trim, and the
// cached full-length filmstrip (so switching clips never regenerates thumbs).
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

    bool trimmed() const {
        return ok && (startSec > 0.0 || endSec < duration);
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

private:
    QVector<Clip> m_clips;
};
