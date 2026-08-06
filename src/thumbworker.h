#pragma once

#include <QImage>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

// Generates the filmstrip thumbnails for a stretch of the video off the UI
// thread. The stretch is the whole video normally, or the zoom window.
class ThumbWorker : public QThread {
    Q_OBJECT

public:
    ThumbWorker(QString path, double startSec, double lenSec, int count, QObject *parent = nullptr)
        : QThread(parent), m_path(std::move(path)), m_start(startSec), m_len(lenSec), m_count(count) {}

    void requestStop();

signals:
    void thumbReady(int index, const QImage &image);

protected:
    void run() override;

private:
    QString m_path;
    double m_start;
    double m_len;
    int m_count;
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
};
