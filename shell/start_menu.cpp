// SPDX-License-Identifier: GPL-3.0-or-later
#include "start_menu.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <algorithm>
#include <gio/gdesktopappinfo.h>
#include <pwd.h>
#include <unistd.h>
#if SHAODESK_DBUS || SHAODESK_TRAY
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#endif

namespace {
QString defaultStateDir() {
    auto state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty() || QDir::isRelativePath(state))
        state = QDir::homePath() + "/.local/state";
    return state + "/shaodesk";
}
} // namespace

StartMenu::StartMenu(QString stateDir, QObject *parent)
    : QObject(parent), stateDir_(stateDir.isEmpty() ? defaultStateDir() : std::move(stateDir)),
      history_(stateDir_ + "/launches") {
    QFile pins(stateDir_ + "/start-pinned");
    if (pins.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ownPins_ = true;
        for (const auto &line : QString::fromUtf8(pins.readAll()).split('\n', Qt::SkipEmptyParts))
            if (!line.trimmed().isEmpty() && !pins_.contains(line.trimmed()))
                pins_.push_back(line.trimmed());
    }
    findUser();
}

StartMenu::~StartMenu() = default;

void StartMenu::setApps(const QVariantList &apps, const QStringList &taskbarPins) {
    apps_ = apps;
    taskbarPins_ = taskbarPins;
    // By section, "#" first, then by name.
    std::vector<std::pair<QString, QVariantMap>> named;
    for (const auto &app : apps) {
        auto record = app.toMap();
        record["letter"] = letterOf(record["name"].toString());
        named.push_back({record["letter"].toString(), record});
    }
    std::stable_sort(named.begin(), named.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first) {
            if (a.first == "#" || b.first == "#")
                return a.first == "#";
            return QString::localeAwareCompare(a.first, b.first) < 0;
        }
        return QString::localeAwareCompare(a.second["name"].toString(), b.second["name"].toString()) < 0;
    });
    sorted_.clear();
    for (const auto &item : named)
        sorted_.push_back(item.second);
    if (!ownPins_) {
        QStringList ids;
        for (const auto &app : apps)
            if (!app.toMap()["configured"].toBool())
                ids.push_back(app.toMap()["appId"].toString());
        pins_ = seed(taskbarPins, commonApps(), ids);
    }
    Q_EMIT appsChanged();
    Q_EMIT pinnedChanged();
    Q_EMIT recentChanged();
}

QVariantMap StartMenu::appRecord(const QString &id) const {
    for (const auto &app : apps_)
        if (app.toMap()["appId"] == id)
            return app.toMap();
    return {};
}

bool StartMenu::installed(const QString &id) const {
    const auto record = appRecord(id);
    return !record.isEmpty() && !record["configured"].toBool();
}

QVariantList StartMenu::pinned() const {
    QVariantList list;
    for (const auto &id : pins_)
        if (installed(id))
            list.push_back(appRecord(id));
    return list;
}

QVariantList StartMenu::recent() const {
    QVariantList list;
    for (const auto &entry : history_.entries()) {
        if (!installed(entry.id))
            continue;
        auto record = appRecord(entry.id);
        record["launches"] = entry.count;
        record["launched"] = entry.last;
        list.push_back(record);
        if (list.size() == 12)
            break;
    }
    return list;
}

void StartMenu::record(const QString &id, const QDateTime &when) {
    if (!installed(id))
        return;
    if (!history_.record(id, when))
        Q_EMIT failed("Could not save the launch history: " + history_.error());
    Q_EMIT recentChanged();
}

void StartMenu::preview(const QStringList &pins, const QList<LaunchHistory::Entry> &launches) {
    previewOnly_ = ownPins_ = true;
    pins_ = pins;
    history_ = LaunchHistory();
    auto ordered = launches;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto &a, const auto &b) { return a.last < b.last; });
    for (const auto &entry : ordered)
        for (int i = 0; i < entry.count; ++i)
            history_.record(entry.id, entry.last);
    Q_EMIT pinnedChanged();
    Q_EMIT recentChanged();
}

void StartMenu::setUser(const QString &name, const QUrl &icon) {
    userSet_ = true;
    userName_ = name;
    userIcon_ = icon;
    Q_EMIT userChanged();
}

bool StartMenu::isPinned(const QString &id) const { return pins_.contains(id); }

void StartMenu::pin(const QString &id) {
    if (!installed(id) || pins_.contains(id))
        return;
    pins_.push_back(id);
    savePins();
}

void StartMenu::unpin(const QString &id) {
    if (!pins_.removeOne(id))
        return;
    savePins();
}

void StartMenu::movePin(const QString &id, const QString &target) {
    const auto from = pins_.indexOf(id), to = pins_.indexOf(target);
    if (from < 0 || to < 0 || from == to)
        return;
    pins_.move(from, to);
    savePins();
}

// The pins are the start menu's own from their first change on.
void StartMenu::savePins() {
    ownPins_ = true;
    Q_EMIT pinnedChanged();
    if (previewOnly_)
        return;
    const auto path = stateDir_ + "/start-pinned";
    QDir().mkpath(stateDir_);
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        for (const auto &id : pins_)
            file.write(id.toUtf8() + '\n');
        if (file.commit())
            return;
    }
    Q_EMIT failed("Could not save the start menu's pins: " + file.errorString());
}

QString StartMenu::ago(const QDateTime &then, const QDateTime &now) const {
    const qint64 seconds = then.secsTo(now);
    if (seconds < 60)
        return "Just now";
    if (seconds < 3600)
        return QString("%1 min ago").arg(seconds / 60);
    const QDate day = then.toLocalTime().date(), today = now.toLocalTime().date();
    if (day == today)
        return seconds < 7200 ? QString("1 hour ago") : QString("%1 hours ago").arg(seconds / 3600);
    if (day.addDays(1) == today)
        return "Yesterday";
    if (day.daysTo(today) < 7)
        return QLocale().dayName(day.dayOfWeek());
    return QLocale().toString(day, day.year() == today.year() ? "d MMM" : "d MMM yyyy");
}

QStringList StartMenu::seed(const QStringList &taskbarPins, const QStringList &common,
                            const QStringList &installed, int wanted) {
    QStringList pins;
    for (const auto &id : taskbarPins)
        if (installed.contains(id) && !pins.contains(id))
            pins.push_back(id);
    for (const auto &id : common)
        if (pins.size() < wanted && installed.contains(id) && !pins.contains(id))
            pins.push_back(id);
    return pins;
}

QStringList StartMenu::commonApps() {
    QStringList ids;
    auto add = [&ids](GAppInfo *info) {
        if (!info)
            return;
        const char *id = g_app_info_get_id(info);
        if (id && g_app_info_should_show(info) && !ids.contains(QString::fromUtf8(id)))
            ids.push_back(QString::fromUtf8(id));
        g_object_unref(info);
    };
    add(g_app_info_get_default_for_type("x-scheme-handler/https", FALSE));
    add(g_app_info_get_default_for_type("inode/directory", FALSE));
    // A terminal opens no type of file; its entry says what it is.
    GList *all = g_app_info_get_all();
    for (GList *item = all; item; item = item->next) {
        auto *info = G_APP_INFO(item->data);
        const char *categories = G_IS_DESKTOP_APP_INFO(info)
                                     ? g_desktop_app_info_get_categories(G_DESKTOP_APP_INFO(info))
                                     : nullptr;
        if (categories && g_app_info_should_show(info) &&
            QString::fromUtf8(categories).split(';').contains("TerminalEmulator")) {
            add(G_APP_INFO(g_object_ref(info)));
            break;
        }
    }
    g_list_free_full(all, g_object_unref);
    for (const char *type : {"text/plain", "x-scheme-handler/mailto", "image/png", "video/mp4"})
        add(g_app_info_get_default_for_type(type, FALSE));
    return ids;
}

QString StartMenu::letterOf(const QString &name) {
    const QString plain = name.trimmed().normalized(QString::NormalizationForm_KD);
    if (plain.isEmpty() || !plain[0].isLetter())
        return "#";
    return plain.left(1).toUpper();
}

void StartMenu::findUser() {
    if (const passwd *entry = getpwuid(getuid())) {
        // The GECOS field: the full name, then the office, phones and the like after commas.
        const auto full = QString::fromLocal8Bit(entry->pw_gecos).section(',', 0, 0).trimmed();
        userName_ = full.isEmpty() ? QString::fromLocal8Bit(entry->pw_name) : full;
    }
    if (userName_.isEmpty())
        userName_ = qEnvironmentVariable("USER");
    for (const char *name : {"/.face", "/.face.icon"}) {
        const auto path = QDir::homePath() + name;
        if (QFileInfo(path).isFile()) {
            userIcon_ = QUrl::fromLocalFile(path);
            return;
        }
    }
#if SHAODESK_DBUS || SHAODESK_TRAY
    // AccountsService keeps the picture a login screen shows. Asked only while it runs: starting
    // it is not the shell's business.
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return;
    auto find = QDBusMessage::createMethodCall("org.freedesktop.Accounts", "/org/freedesktop/Accounts",
                                               "org.freedesktop.Accounts", "FindUserById");
    find << qlonglong(getuid());
    find.setAutoStartService(false);
    auto *found = new QDBusPendingCallWatcher(bus.asyncCall(find), this);
    connect(found, &QDBusPendingCallWatcher::finished, this, [this, bus](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        QDBusPendingReply<QDBusObjectPath> user = *call;
        if (user.isError() || userSet_)
            return;
        auto get = QDBusMessage::createMethodCall("org.freedesktop.Accounts", user.value().path(),
                                                  "org.freedesktop.DBus.Properties", "Get");
        get << QString("org.freedesktop.Accounts.User") << QString("IconFile");
        get.setAutoStartService(false);
        auto *read = new QDBusPendingCallWatcher(bus.asyncCall(get), this);
        connect(read, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
            call->deleteLater();
            QDBusPendingReply<QDBusVariant> icon = *call;
            const auto path = icon.isError() ? QString() : icon.value().variant().toString();
            if (userSet_ || !QFileInfo(path).isFile())
                return;
            userIcon_ = QUrl::fromLocalFile(path);
            Q_EMIT userChanged();
        });
    });
#endif
}
