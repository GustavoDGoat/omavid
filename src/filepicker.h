#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openVideo() = 0;
    virtual void openAudio() = 0;
    // scaleHeights are the downscale choices to offer besides "Original"
    // (e.g. {1080, 720}), matched by min(width, height) of the source.
    virtual void exportVideo(const QUrl &suggestedUrl, double start, double end,
                             const QList<int> &scaleHeights) = 0;
    virtual void exportMerged(const QUrl &suggestedUrl,
                              const QList<int> &scaleHeights) = 0;

signals:
    void openSelected(const QList<QUrl> &urls);
    void audioSelected(const QUrl &url);
    // scaleHeight is 0 for "Original", otherwise the chosen short-side size.
    void exportSelected(const QUrl &url, double start, double end, int scaleHeight);
    void exportMergedSelected(const QUrl &url, int scaleHeight);
    void failed(const QString &message);
};
