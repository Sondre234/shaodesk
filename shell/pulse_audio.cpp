// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"
#include <QIcon>
#include <QTimer>
#include <map>
#include <pulse/pulseaudio.h>
#include <string>

namespace {
int percent(const pa_cvolume &volume) {
    return int((uint64_t(pa_cvolume_max(&volume)) * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
}
pa_volume_t level(int percent) { return pa_volume_t(uint64_t(percent) * PA_VOLUME_NORM / 100); }
QString propertyOf(const pa_proplist *list, const char *key) {
    const char *value = pa_proplist_gets(list, key);
    return value ? QString::fromUtf8(value) : QString();
}

// The server runs its own thread: its callbacks gather a whole snapshot (server, sinks, sink
// inputs) under the main loop's lock and hand it to the Qt thread in one piece. Requests from
// the Qt thread take the same lock.
class PulseAudio : public Audio {
  public:
    PulseAudio() {
        loop_ = pa_threaded_mainloop_new();
        connectServer();
        pa_threaded_mainloop_start(loop_);
    }
    ~PulseAudio() override {
        pa_threaded_mainloop_stop(loop_);
        dropContext();
        pa_threaded_mainloop_free(loop_);
    }

  protected:
    void sendVolume(const QString &output, int value) override {
        Locked lock(loop_);
        auto found = sinkVolumes_.find(output.toStdString());
        if (!ready() || found == sinkVolumes_.end())
            return;
        pa_cvolume_scale(&found->second, level(value));
        run(pa_context_set_sink_volume_by_name(context_, found->first.c_str(), &found->second,
                                               nullptr, nullptr));
    }
    void sendMute(const QString &output, bool muted) override {
        Locked lock(loop_);
        if (ready())
            run(pa_context_set_sink_mute_by_name(context_, output.toUtf8().constData(), muted,
                                                 nullptr, nullptr));
    }
    void sendOutput(const QString &output, const std::vector<uint32_t> &streams) override {
        Locked lock(loop_);
        if (!ready())
            return;
        const auto name = output.toUtf8();
        run(pa_context_set_default_sink(context_, name.constData(), nullptr, nullptr));
        for (auto id : streams)
            run(pa_context_move_sink_input_by_name(context_, id, name.constData(), nullptr,
                                                   nullptr));
    }
    void sendStreamVolume(uint32_t id, int value) override {
        Locked lock(loop_);
        auto found = streamVolumes_.find(id);
        if (!ready() || found == streamVolumes_.end())
            return;
        pa_cvolume_scale(&found->second, level(value));
        run(pa_context_set_sink_input_volume(context_, id, &found->second, nullptr, nullptr));
    }
    void sendStreamMute(uint32_t id, bool muted) override {
        Locked lock(loop_);
        if (ready())
            run(pa_context_set_sink_input_mute(context_, id, muted, nullptr, nullptr));
    }

  private:
    struct Locked {
        pa_threaded_mainloop *loop;
        explicit Locked(pa_threaded_mainloop *l) : loop(l) { pa_threaded_mainloop_lock(loop); }
        ~Locked() { pa_threaded_mainloop_unlock(loop); }
    };
    // What the server thread has read so far; icons are looked up in the Qt thread.
    struct Snapshot {
        State state;
        std::vector<QStringList> iconCandidates;
        std::map<std::string, pa_cvolume> sinkVolumes;
        std::map<uint32_t, pa_cvolume> streamVolumes;
    };
    pa_threaded_mainloop *loop_ = nullptr;
    pa_context *context_ = nullptr;
    Snapshot pending_;
    int outstanding_ = 0;
    bool again_ = false;
    // The volumes last read, so a change keeps each channel's balance.
    std::map<std::string, pa_cvolume> sinkVolumes_;
    std::map<uint32_t, pa_cvolume> streamVolumes_;

    bool ready() const { return context_ && pa_context_get_state(context_) == PA_CONTEXT_READY; }
    static void run(pa_operation *operation) {
        if (operation)
            pa_operation_unref(operation);
    }
    void dropContext() {
        if (!context_)
            return;
        pa_context_set_state_callback(context_, nullptr, nullptr);
        pa_context_set_subscribe_callback(context_, nullptr, nullptr);
        pa_context_disconnect(context_);
        pa_context_unref(context_);
        context_ = nullptr;
    }
    // (Re)connects from the Qt thread. NOFAIL waits for a server that is not running yet.
    void connectServer() {
        Locked lock(loop_);
        dropContext();
        auto *props = pa_proplist_new();
        pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "shaoDe");
        pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "shaode-shell");
        pa_proplist_sets(props, PA_PROP_APPLICATION_ICON_NAME, "audio-volume-high");
        context_ =
            pa_context_new_with_proplist(pa_threaded_mainloop_get_api(loop_), "shaoDe", props);
        pa_proplist_free(props);
        outstanding_ = 0;
        again_ = false;
        pa_context_set_state_callback(context_, &PulseAudio::onState, this);
        if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFAIL, nullptr) < 0)
            lost();
    }
    // A server that went away (a PipeWire restart) is waited for again.
    void lost() {
        QMetaObject::invokeMethod(
            this,
            [this] {
                setUnavailable();
                QTimer::singleShot(2000, this, [this] { connectServer(); });
            },
            Qt::QueuedConnection);
    }
    static void onState(pa_context *context, void *data) {
        auto *self = static_cast<PulseAudio *>(data);
        switch (pa_context_get_state(context)) {
        case PA_CONTEXT_READY:
            pa_context_set_subscribe_callback(context, &PulseAudio::onEvent, self);
            run(pa_context_subscribe(context,
                                     pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK |
                                                            PA_SUBSCRIPTION_MASK_SINK_INPUT |
                                                            PA_SUBSCRIPTION_MASK_SERVER),
                                     nullptr, nullptr));
            self->refresh();
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            self->lost();
            break;
        default:
            break;
        }
    }
    static void onEvent(pa_context *, pa_subscription_event_type_t, uint32_t, void *data) {
        static_cast<PulseAudio *>(data)->refresh();
    }
    // Reads everything again; events that arrive meanwhile ask for one more read afterwards.
    void refresh() {
        if (outstanding_ > 0) {
            again_ = true;
            return;
        }
        pending_ = {};
        outstanding_ = 3;
        run(pa_context_get_server_info(context_, &PulseAudio::onServer, this));
        run(pa_context_get_sink_info_list(context_, &PulseAudio::onSink, this));
        run(pa_context_get_sink_input_info_list(context_, &PulseAudio::onStream, this));
    }
    static void onServer(pa_context *, const pa_server_info *info, void *data) {
        auto *self = static_cast<PulseAudio *>(data);
        if (info && info->default_sink_name)
            self->pending_.state.output = QString::fromUtf8(info->default_sink_name);
        self->finish();
    }
    static void onSink(pa_context *, const pa_sink_info *info, int end, void *data) {
        auto *self = static_cast<PulseAudio *>(data);
        if (end) {
            self->finish();
            return;
        }
        self->pending_.state.outputs.push_back({QString::fromUtf8(info->name),
                                                QString::fromUtf8(info->description),
                                                percent(info->volume), bool(info->mute)});
        self->pending_.sinkVolumes[info->name] = info->volume;
    }
    static void onStream(pa_context *, const pa_sink_input_info *info, int end, void *data) {
        auto *self = static_cast<PulseAudio *>(data);
        if (end) {
            self->finish();
            return;
        }
        if (!info->has_volume)
            return;
        auto name = propertyOf(info->proplist, PA_PROP_APPLICATION_NAME);
        if (name.isEmpty())
            name = QString::fromUtf8(info->name);
        self->pending_.state.streams.push_back(
            {info->index, name, {}, percent(info->volume), bool(info->mute)});
        self->pending_.iconCandidates.push_back(
            {propertyOf(info->proplist, PA_PROP_APPLICATION_ICON_NAME),
             propertyOf(info->proplist, PA_PROP_APPLICATION_PROCESS_BINARY), name.toLower()});
        self->pending_.streamVolumes[info->index] = info->volume;
    }
    void finish() {
        if (--outstanding_ > 0)
            return;
        sinkVolumes_ = pending_.sinkVolumes;
        streamVolumes_ = pending_.streamVolumes;
        QMetaObject::invokeMethod(
            this,
            [this, snapshot = std::move(pending_)]() mutable {
                for (size_t i = 0; i < snapshot.state.streams.size(); ++i) {
                    auto &icon = snapshot.state.streams[i].icon;
                    for (const auto &candidate : snapshot.iconCandidates[i])
                        if (!candidate.isEmpty() && QIcon::hasThemeIcon(candidate)) {
                            icon = candidate;
                            break;
                        }
                    if (icon.isEmpty())
                        icon = "audio-x-generic";
                }
                update(std::move(snapshot.state));
            },
            Qt::QueuedConnection);
        if (again_) {
            again_ = false;
            refresh();
        }
    }
};
} // namespace

std::unique_ptr<Audio> makeAudio() { return std::make_unique<PulseAudio>(); }
