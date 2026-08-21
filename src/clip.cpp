#include "clip.h"

ClipListModel::ClipListModel(QObject *parent) : QAbstractListModel(parent) {}

int ClipListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_clips.size();
}

QVariant ClipListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_clips.size())
        return {};
    const Clip &clip = m_clips.at(index.row());
    switch (role) {
    case UrlRole: return clip.url;
    case NameRole: return clip.name;
    case DurationRole: return clip.duration;
    case StartRole: return clip.startSec;
    case EndRole: return clip.endSec;
    case OkRole: return clip.ok;
    case ErrorRole: return clip.error;
    case TrimmedRole: return clip.trimmed();
    case HasAudioRole: return clip.hasAudio();
    case AudioNameRole: return clip.audioName;
    case AudioStartRole: return clip.audioStartSec;
    case AudioEndRole: return clip.audioEndSec;
    case AudioDurationRole: return clip.audioDuration;
    default: return {};
    }
}

bool ClipListModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_clips.size())
        return false;
    if (role == StartRole || role == EndRole) {
        Clip &clip = m_clips[index.row()];
        const double v = value.toDouble();
        if (role == StartRole)
            clip.startSec = v;
        else
            clip.endSec = v;
        emit dataChanged(index, index, {StartRole, EndRole, TrimmedRole});
        return true;
    }
    return false;
}

QHash<int, QByteArray> ClipListModel::roleNames() const {
    return {
        {UrlRole, "url"},
        {NameRole, "name"},
        {DurationRole, "duration"},
        {StartRole, "startSec"},
        {EndRole, "endSec"},
        {OkRole, "ok"},
        {ErrorRole, "error"},
        {TrimmedRole, "trimmed"},
        {HasAudioRole, "hasAudio"},
        {AudioNameRole, "audioName"},
        {AudioStartRole, "audioStart"},
        {AudioEndRole, "audioEnd"},
        {AudioDurationRole, "audioDuration"},
    };
}

int ClipListModel::append(const Clip &clip) {
    const int row = m_clips.size();
    beginInsertRows(QModelIndex(), row, row);
    m_clips.append(clip);
    endInsertRows();
    return row;
}

void ClipListModel::removeAt(int index) {
    if (index < 0 || index >= m_clips.size())
        return;
    beginRemoveRows(QModelIndex(), index, index);
    m_clips.removeAt(index);
    endRemoveRows();
}

void ClipListModel::move(int from, int to) {
    if (from < 0 || from >= m_clips.size() || to < 0 || to >= m_clips.size() || from == to)
        return;
    // beginMoveRows' destination index is where the row ends up; Qt's QVector
    // move() treats `to` the same way, so offset for a downward move.
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    m_clips.move(from, to);
    endMoveRows();
}

bool ClipListModel::setTrim(int index, double start, double end) {
    if (index < 0 || index >= m_clips.size())
        return false;
    Clip &clip = m_clips[index];
    if (qFuzzyCompare(clip.startSec, start) && qFuzzyCompare(clip.endSec, end))
        return false;
    clip.startSec = start;
    clip.endSec = end;
    const QModelIndex idx = this->index(index);
    emit dataChanged(idx, idx, {StartRole, EndRole, TrimmedRole});
    return true;
}

bool ClipListModel::setAudio(int index, const QString &path, const QString &name, double duration,
                             double start, double end) {
    if (index < 0 || index >= m_clips.size())
        return false;
    Clip &clip = m_clips[index];
    clip.audioPath = path;
    clip.audioName = name;
    clip.audioDuration = duration;
    clip.audioStartSec = start;
    clip.audioEndSec = end;
    const QModelIndex idx = this->index(index);
    emit dataChanged(idx, idx, {HasAudioRole, AudioNameRole, AudioStartRole,
                                AudioEndRole, AudioDurationRole});
    return true;
}

bool ClipListModel::clearAudio(int index) {
    if (index < 0 || index >= m_clips.size())
        return false;
    Clip &clip = m_clips[index];
    if (clip.audioPath.isEmpty())
        return false;
    clip.audioPath.clear();
    clip.audioName.clear();
    clip.audioDuration = 0.0;
    clip.audioStartSec = 0.0;
    clip.audioEndSec = 0.0;
    const QModelIndex idx = this->index(index);
    emit dataChanged(idx, idx, {HasAudioRole, AudioNameRole, AudioStartRole,
                                AudioEndRole, AudioDurationRole});
    return true;
}

AudioTrackModel::AudioTrackModel(QObject *parent) : QAbstractListModel(parent) {}

int AudioTrackModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_clips.size();
}

QVariant AudioTrackModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_clips.size())
        return {};
    const AudioClip &clip = m_clips.at(index.row());
    switch (role) {
    case NameRole: return clip.name;
    case DurationRole: return clip.duration;
    case StartRole: return clip.startSec;
    case EndRole: return clip.endSec;
    case PositionRole: return clip.positionSec;
    case TrimmedRole: return clip.trimmed();
    default: return {};
    }
}

bool AudioTrackModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_clips.size())
        return false;
    AudioClip &clip = m_clips[index.row()];
    const double v = value.toDouble();
    if (role == StartRole) {
        clip.startSec = v;
    } else if (role == EndRole) {
        clip.endSec = v;
    } else if (role == PositionRole) {
        clip.positionSec = v;
    } else {
        return false;
    }
    emit dataChanged(index, index, {role, TrimmedRole});
    return true;
}

QHash<int, QByteArray> AudioTrackModel::roleNames() const {
    return {
        {NameRole, "name"},
        {DurationRole, "duration"},
        {StartRole, "startSec"},
        {EndRole, "endSec"},
        {PositionRole, "positionSec"},
        {TrimmedRole, "trimmed"},
    };
}

int AudioTrackModel::append(const AudioClip &clip) {
    const int row = m_clips.size();
    beginInsertRows(QModelIndex(), row, row);
    m_clips.append(clip);
    endInsertRows();
    return row;
}

void AudioTrackModel::removeAt(int index) {
    if (index < 0 || index >= m_clips.size())
        return;
    beginRemoveRows(QModelIndex(), index, index);
    m_clips.removeAt(index);
    endRemoveRows();
}

bool AudioTrackModel::setTrim(int index, double start, double end) {
    if (index < 0 || index >= m_clips.size())
        return false;
    AudioClip &clip = m_clips[index];
    if (qFuzzyCompare(clip.startSec, start) && qFuzzyCompare(clip.endSec, end))
        return false;
    clip.startSec = start;
    clip.endSec = end;
    const QModelIndex idx = this->index(index);
    emit dataChanged(idx, idx, {StartRole, EndRole, TrimmedRole});
    return true;
}

bool AudioTrackModel::setPosition(int index, double position) {
    if (index < 0 || index >= m_clips.size())
        return false;
    AudioClip &clip = m_clips[index];
    if (qFuzzyCompare(clip.positionSec, position))
        return false;
    clip.positionSec = position;
    const QModelIndex idx = this->index(index);
    emit dataChanged(idx, idx, {PositionRole});
    return true;
}
