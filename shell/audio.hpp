// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QObject>
#include <QVariantList>
#include <cstdint>
#include <memory>
#include <vector>

// The applications playing sound, one row each, updated in place so a slider being dragged
// is not rebuilt under the pointer.
class AudioStreams : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    struct Stream {
        uint32_t id = 0;
        QString name, icon;
        int volume = 0;
        bool muted = false;
    };
    enum Role { IdRole = Qt::UserRole + 1, NameRole, IconRole, VolumeRole, MutedRole };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(streams_.size()); }
    void update(std::vector<Stream> streams);
    Stream *find(uint32_t id);
    void changed(uint32_t id);
  Q_SIGNALS:
    void countChanged();

  private:
    std::vector<Stream> streams_;
};

// The sound server as the panel shows it: the default output's volume, the outputs to choose
// from, and the applications playing. Volumes are percentages. A backend delivers the state
// through update() and carries out the send*() requests; changes show at once, before the
// server confirms them, so successive wheel steps build on each other.
class Audio : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(QString output READ output NOTIFY changed)
    // [{name, description}], in the server's order.
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(AudioStreams *streams READ streams CONSTANT)
  public:
    struct Output {
        QString name, description;
        int volume = 0;
        bool muted = false;
    };
    struct State {
        QString output;
        std::vector<Output> outputs;
        std::vector<AudioStreams::Stream> streams;
    };
    static constexpr int maxVolume = 100;
    explicit Audio(QObject *parent = nullptr) : QObject(parent), streams_(this) {}
    bool available() const { return available_; }
    int volume() const;
    bool muted() const;
    QString output() const { return state_.output; }
    QVariantList outputs() const;
    AudioStreams *streams() { return &streams_; }
    void update(State state);
    void setUnavailable();
    Q_INVOKABLE void setVolume(int percent);
    // Steps the default output's volume, as the wheel does.
    Q_INVOKABLE void changeVolume(int delta);
    Q_INVOKABLE void toggleMute();
    // Makes `name` the default output and moves the playing applications to it.
    Q_INVOKABLE void setOutput(const QString &name);
    Q_INVOKABLE void setStreamVolume(int id, int percent);
    Q_INVOKABLE void toggleStreamMute(int id);
  Q_SIGNALS:
    void changed();

  protected:
    virtual void sendVolume(const QString &output, int percent) = 0;
    virtual void sendMute(const QString &output, bool muted) = 0;
    virtual void sendOutput(const QString &output, const std::vector<uint32_t> &streams) = 0;
    virtual void sendStreamVolume(uint32_t id, int percent) = 0;
    virtual void sendStreamMute(uint32_t id, bool muted) = 0;

  private:
    bool available_ = false;
    State state_;
    AudioStreams streams_;
    Output *current();
    const Output *current() const;
};

// The PulseAudio (or PipeWire's pulse) backend, or one that stays unavailable when shaodesk was
// built without libpulse.
std::unique_ptr<Audio> makeAudio();
