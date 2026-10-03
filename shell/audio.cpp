// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"
#include <algorithm>

int AudioStreams::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(streams_.size());
}
QVariant AudioStreams::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= int(streams_.size()))
        return {};
    const auto &stream = streams_[index.row()];
    switch (role) {
    case IdRole:
        return int(stream.id);
    case NameRole:
        return stream.name;
    case IconRole:
        return stream.icon;
    case VolumeRole:
        return stream.volume;
    case MutedRole:
        return stream.muted;
    }
    return {};
}
QHash<int, QByteArray> AudioStreams::roleNames() const {
    return {{IdRole, "streamId"},
            {NameRole, "name"},
            {IconRole, "icon"},
            {VolumeRole, "volume"},
            {MutedRole, "muted"}};
}
// Keeps the rows of streams that are still playing and appends the new ones.
void AudioStreams::update(std::vector<Stream> streams) {
    auto listed = [](auto &list, uint32_t id) {
        return std::find_if(list.begin(), list.end(), [id](auto &s) { return s.id == id; });
    };
    const int before = count();
    for (int row = count() - 1; row >= 0; --row)
        if (listed(streams, streams_[row].id) == streams.end()) {
            beginRemoveRows({}, row, row);
            streams_.erase(streams_.begin() + row);
            endRemoveRows();
        }
    for (auto &stream : streams) {
        auto existing = listed(streams_, stream.id);
        if (existing == streams_.end()) {
            beginInsertRows({}, count(), count());
            streams_.push_back(std::move(stream));
            endInsertRows();
        } else if (existing->name != stream.name || existing->icon != stream.icon ||
                   existing->volume != stream.volume || existing->muted != stream.muted) {
            *existing = std::move(stream);
            changed(existing->id);
        }
    }
    if (count() != before)
        Q_EMIT countChanged();
}
AudioStreams::Stream *AudioStreams::find(uint32_t id) {
    for (auto &stream : streams_)
        if (stream.id == id)
            return &stream;
    return nullptr;
}
void AudioStreams::changed(uint32_t id) {
    for (int row = 0; row < count(); ++row)
        if (streams_[row].id == id)
            Q_EMIT dataChanged(index(row), index(row));
}

Audio::Output *Audio::current() {
    for (auto &output : state_.outputs)
        if (output.name == state_.output)
            return &output;
    return nullptr;
}
const Audio::Output *Audio::current() const { return const_cast<Audio *>(this)->current(); }
int Audio::volume() const { return current() ? current()->volume : 0; }
bool Audio::muted() const { return current() && current()->muted; }
QVariantList Audio::outputs() const {
    QVariantList list;
    for (const auto &output : state_.outputs)
        list.push_back(QVariantMap{{"name", output.name}, {"description", output.description}});
    return list;
}
void Audio::update(State state) {
    streams_.update(std::move(state.streams));
    state_ = std::move(state);
    available_ = true;
    Q_EMIT changed();
}
void Audio::setUnavailable() {
    streams_.update({});
    state_ = {};
    available_ = false;
    Q_EMIT changed();
}
void Audio::setVolume(int percent) {
    auto *output = current();
    percent = std::clamp(percent, 0, maxVolume);
    if (!output || output->volume == percent)
        return;
    output->volume = percent;
    sendVolume(output->name, percent);
    Q_EMIT changed();
}
void Audio::changeVolume(int delta) { setVolume(volume() + delta); }
void Audio::toggleMute() {
    if (auto *output = current()) {
        output->muted = !output->muted;
        sendMute(output->name, output->muted);
        Q_EMIT changed();
    }
}
void Audio::setOutput(const QString &name) {
    if (name == state_.output)
        return;
    std::vector<uint32_t> ids;
    for (int row = 0; row < streams_.count(); ++row)
        ids.push_back(streams_.data(streams_.index(row), AudioStreams::IdRole).toUInt());
    state_.output = name;
    sendOutput(name, ids);
    Q_EMIT changed();
}
void Audio::setStreamVolume(int id, int percent) {
    auto *stream = streams_.find(uint32_t(id));
    percent = std::clamp(percent, 0, maxVolume);
    if (!stream || stream->volume == percent)
        return;
    stream->volume = percent;
    sendStreamVolume(stream->id, percent);
    streams_.changed(stream->id);
}
void Audio::toggleStreamMute(int id) {
    if (auto *stream = streams_.find(uint32_t(id))) {
        stream->muted = !stream->muted;
        sendStreamMute(stream->id, stream->muted);
        streams_.changed(stream->id);
    }
}

#if !SHAODE_PULSE
namespace {
class NoAudio : public Audio {
  public:
    using Audio::Audio;

  protected:
    void sendVolume(const QString &, int) override {}
    void sendMute(const QString &, bool) override {}
    void sendOutput(const QString &, const std::vector<uint32_t> &) override {}
    void sendStreamVolume(uint32_t, int) override {}
    void sendStreamMute(uint32_t, bool) override {}
};
} // namespace
std::unique_ptr<Audio> makeAudio() { return std::make_unique<NoAudio>(); }
#endif
