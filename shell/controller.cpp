// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"
#include "icons.hpp"
#include "notification_images.hpp"
#include "tray_images.hpp"
#include "wallpapers.hpp"
#include <QQmlContext>
#include <QQmlEngine>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <gio/gdesktopappinfo.h>
#include <iostream>
#include <memory>
#if SHAODESK_DBUS
#include "notification_service.hpp"
#endif
#if SHAODESK_TRAY
#include "tray_host.hpp"
#endif

ShellController::ShellController(std::filesystem::path path, QObject *parent)
    : QObject(parent), path_(std::move(path)), tasks_(this) {
    loadConfig();
    QFile pins(pinsPath());
    if (pins.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const auto &line : QString::fromUtf8(pins.readAll()).split('\n', Qt::SkipEmptyParts))
            if (!userPins_.contains(line.trimmed()))
                userPins_.push_back(line.trimmed());
    // One line per profile: its name, the configured wallpaper and the picked one, by tabs.
    QFile picked(pickedWallpapersPath());
    if (picked.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const auto &line : QString::fromUtf8(picked.readAll()).split('\n', Qt::SkipEmptyParts)) {
            const auto fields = line.split('\t');
            if (fields.size() == 3 && !fields[2].isEmpty())
                pickedWallpapers_[fields[0]] = {fields[1], fields[2]};
        }
    refreshApps();
    subscribe();
    notifications_.configure(config_.notifications);
    osd_.configure(config_.osd);
    power_.setCountdown(config_.power.countdown);
    connect(&power_, &Power::failed, this, &ShellController::report);
    connect(notifications_.cards(), &NotificationModel::countChanged, this,
            &ShellController::updateCards);
    connect(&notifications_, &NotificationCenter::received, this, &ShellController::updateCards);
    connect(audio_.get(), &Audio::changed, this, &ShellController::showVolume);
    connect(&backlight_, &Backlight::changed, this, [this](int percent) {
        if (config_.osd.brightness)
            osd_.show(overlayOutput(), "Brightness", percent, "brightness");
    });
}
ShellController::~ShellController() {
    delete engine_; // Before the objects its context refers to go away.
    delete trayHost_; // Before the model it fills.
    clearApps();
}
QQmlEngine *ShellController::engine() {
    if (!engine_) {
        engine_ = new QQmlEngine(this);
        engine_->addImageProvider("icons", new Icons);
        engine_->addImageProvider("notify", new NotificationImages(notifications_));
        engine_->addImageProvider("tray", new TrayImages(tray_));
        engine_->addImageProvider("thumbs", new Thumbnails);
        engine_->rootContext()->setContextProperty("shell", this);
    }
    return engine_;
}
namespace {
// Configuration colors are #RRGGBB or CSS-style #RRGGBBAA; Qt reads eight digits as #AARRGGBB.
QColor color(const std::string &value) {
    auto result = QColor::fromString(QString::fromStdString(value.substr(0, 7)));
    if (value.size() == 9)
        result.setAlpha(std::stoi(value.substr(7), nullptr, 16));
    return result;
}
} // namespace
QColor ShellController::accent() const { return color(config_.shell.accent); }
QColor ShellController::urgentColor() const {
    const auto &c = config_.settings.urgent_color; // premultiplied
    const float a = c[3];
    return a > 0 ? QColor::fromRgbF(c[0] / a, c[1] / a, c[2] / a, a) : QColor();
}
QColor ShellController::panelColor() const { return color(config_.shell.panel_color); }
QColor ShellController::textColor() const { return color(config_.shell.text_color); }
QColor ShellController::background() const {
    return QColor::fromRgbF(config_.settings.background[0], config_.settings.background[1],
                            config_.settings.background[2]);
}
QString ShellController::wallpaperFile() const {
    const auto configured = QString::fromStdString(config_.shell.wallpaper);
    auto it = pickedWallpapers_.find(profile());
    if (it != pickedWallpapers_.end() && it->configured == configured)
        return it->picked;
    if (configured.isEmpty())
        return {};
    std::filesystem::path file(config_.shell.wallpaper);
    if (file.is_relative())
        file = std::filesystem::absolute(path_).parent_path() / file;
    return QString::fromStdString(file.string());
}
QUrl ShellController::wallpaper() const {
    const auto file = wallpaperFile();
    return file.isEmpty() ? QUrl() : QUrl::fromLocalFile(file);
}
QString ShellController::wallpaperFolder() const {
    auto folder = QString::fromStdString(config_.shell.wallpapers);
    if (folder.startsWith("~/"))
        return QDir::homePath() + folder.mid(1);
    if (!folder.isEmpty())
        return folder;
    const auto pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QFileInfo(pictures + "/wallpapers").isDir() ? pictures + "/wallpapers" : pictures;
}
void ShellController::findWallpapers() {
    wallpapers_ = ::findWallpapers(wallpaperFolder());
    Q_EMIT wallpapersChanged();
}
QString ShellController::pickedWallpapersPath() {
    return QFileInfo(pinsPath()).path() + "/wallpapers";
}
void ShellController::pickWallpaper(const QString &path) {
    if (path.isEmpty())
        pickedWallpapers_.remove(profile());
    else
        pickedWallpapers_[profile()] = {QString::fromStdString(config_.shell.wallpaper), path};
    const auto file = pickedWallpapersPath();
    QDir().mkpath(QFileInfo(file).path());
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        for (auto it = pickedWallpapers_.begin(); it != pickedWallpapers_.end(); ++it)
            out.write((it.key() + '\t' + it->configured + '\t' + it->picked + '\n').toUtf8());
        if (!out.commit())
            report("Could not save the wallpaper: " + out.errorString());
    }
    Q_EMIT wallpaperChanged();
}
void ShellController::clearApps() {
    for (auto &app : apps_)
        if (app.info)
            g_object_unref(app.info);
    apps_.clear();
}
void ShellController::refreshApps() {
    clearApps();
    int index = 0;
    for (const auto &launcher : config_.shell.launchers)
        apps_.push_back({QString("pinned:%1").arg(index++), QString::fromStdString(launcher.name),
                         QString::fromStdString(launcher.icon), launcher.command, nullptr, true, {}});
    GList *list = g_app_info_get_all();
    for (GList *item = list; item; item = item->next) {
        auto *info = G_APP_INFO(item->data);
        if (!g_app_info_should_show(info) || !g_app_info_get_id(info))
            continue;
        QString icon = "application-x-executable";
        GIcon *gicon = g_app_info_get_icon(info);
        if (gicon && G_IS_THEMED_ICON(gicon)) {
            const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(gicon));
            if (names && *names)
                icon = QString::fromUtf8(*names);
        } else if (gicon && G_IS_FILE_ICON(gicon)) {
            char *path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(gicon)));
            if (path) {
                icon = QString::fromUtf8(path);
                g_free(path);
            }
        }
        const auto id = QString::fromUtf8(g_app_info_get_id(info));
        const char *wmClass = G_IS_DESKTOP_APP_INFO(info)
                                  ? g_desktop_app_info_get_startup_wm_class(G_DESKTOP_APP_INFO(info))
                                  : nullptr;
        apps_.push_back({id, QString::fromUtf8(g_app_info_get_display_name(info)), icon, {},
                         G_APP_INFO(g_object_ref(info)), userPins_.contains(id),
                         QString::fromUtf8(wmClass ? wmClass : "")});
    }
    g_list_free_full(list, g_object_unref);
    sortApps();
    Q_EMIT appsChanged();
}
// Configured launchers first, then applications pinned from the shell in the order they were
// pinned, then everything else by name.
void ShellController::sortApps() {
    auto rank = [this](const App &app) {
        if (!app.command.empty())
            return -1;
        return app.pinned ? int(userPins_.indexOf(app.id)) : int(userPins_.size());
    };
    std::stable_sort(apps_.begin(), apps_.end(), [&rank](const App &a, const App &b) {
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        if (a.pinned)
            return false;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
}
QString ShellController::pinsPath() {
    auto state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty() || QDir::isRelativePath(state))
        state = QDir::homePath() + "/.local/state";
    return state + "/shaodesk/pinned";
}
void ShellController::savePins() {
    const auto path = pinsPath();
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        for (const auto &id : userPins_)
            file.write(id.toUtf8() + '\n');
        if (file.commit())
            return;
    }
    report("Could not save pinned applications: " + file.errorString());
}
void ShellController::pin(const QString &id) {
    auto it = std::find_if(apps_.begin(), apps_.end(),
                           [&id](const App &app) { return app.id == id && app.info; });
    if (it == apps_.end() || it->pinned)
        return;
    it->pinned = true;
    userPins_.push_back(id);
    savePins();
    sortApps();
    Q_EMIT appsChanged();
}
void ShellController::unpin(const QString &id) {
    auto it = std::find_if(apps_.begin(), apps_.end(),
                           [&id](const App &app) { return app.id == id && app.info; });
    if (!userPins_.removeOne(id))
        return;
    if (it != apps_.end())
        it->pinned = false;
    savePins();
    sortApps();
    Q_EMIT appsChanged();
}
void ShellController::movePin(const QString &id, const QString &target) {
    const auto from = userPins_.indexOf(id);
    auto to = userPins_.indexOf(target);
    // Dropped on a configured launcher: first after them.
    if (to < 0 &&
        std::any_of(
            apps_.begin(), apps_.end(),
            [&target](const App &app) { return app.id == target && !app.command.empty(); }))
        to = 0;
    if (from < 0 || to < 0 || from == to)
        return;
    userPins_.move(from, to);
    savePins();
    sortApps();
    Q_EMIT appsChanged();
}
bool ShellController::isPinned(const QString &id) const {
    return std::any_of(apps_.begin(), apps_.end(),
                       [&id](const App &app) { return app.id == id && app.pinned; });
}
QString ShellController::appFor(const QString &windowAppId) const {
    if (windowAppId.isEmpty())
        return {};
    // Most windows use their desktop file's name; others, like many X11 clients, only its
    // StartupWMClass or the last part of a reverse-DNS name.
    auto base = [](const QString &id) { return id.endsWith(".desktop") ? id.chopped(8) : id; };
    const std::function<bool(const App &)> matches[] = {
        [&](const App &app) { return base(app.id) == windowAppId; },
        [&](const App &app) { return base(app.id).compare(windowAppId, Qt::CaseInsensitive) == 0; },
        [&](const App &app) {
            return !app.wmClass.isEmpty() &&
                   app.wmClass.compare(windowAppId, Qt::CaseInsensitive) == 0;
        },
        [&](const App &app) {
            return base(app.id).section('.', -1).compare(windowAppId, Qt::CaseInsensitive) == 0;
        },
    };
    for (const auto &match : matches)
        for (const auto &app : apps_)
            if (app.info && match(app))
                return app.id;
    return {};
}
QString ShellController::pinnedAppFor(const QString &windowAppId) const {
    if (windowAppId.isEmpty())
        return {};
    // A configured launcher has no desktop file; its window is known by the program it runs
    // (kitty's app id is "kitty") or by an icon named after the application.
    for (const auto &app : apps_)
        if (!app.command.empty() &&
            (QFileInfo(QString::fromStdString(app.command.front()))
                     .fileName()
                     .compare(windowAppId, Qt::CaseInsensitive) == 0 ||
             app.icon.compare(windowAppId, Qt::CaseInsensitive) == 0))
            return app.id;
    auto id = appFor(windowAppId);
    return isPinned(id) ? id : QString();
}
QVariantMap ShellController::record(const App &app) {
    return {{"appId", app.id},
            {"name", app.name},
            {"icon", app.icon},
            {"pinned", app.pinned},
            {"configured", !app.command.empty()}};
}
QVariantList ShellController::apps() const {
    QVariantList list;
    for (const auto &app : apps_)
        list.push_back(record(app));
    return list;
}
QVariantMap ShellController::widgets() const {
    const auto &w = config_.shell.widgets;
    return {{"workspaces", w.workspaces}, {"battery", w.battery}, {"network", w.network},
            {"volume", w.volume},         {"clock", w.clock},     {"calendar", w.calendar},
            {"tiling", w.tiling},         {"profiles", w.profiles},
            {"wallpapers", w.wallpapers}, {"keyboard_layout", w.keyboard_layout},
            {"power", w.power}};
}
QStringList ShellController::profiles() const {
    QStringList names;
    for (const auto &name : config_.profiles)
        names.push_back(QString::fromStdString(name));
    return names;
}
QVariantList ShellController::pinned() const {
    QVariantList list;
    for (const auto &app : apps_)
        if (app.pinned)
            list.push_back(record(app));
    return list;
}
void ShellController::report(const QString &message) {
    error_ = message;
    Q_EMIT errorChanged();
    QTimer::singleShot(8000, this, [this, message] {
        if (error_ == message)
            clearError();
    });
}
void ShellController::clearError() {
    error_.clear();
    Q_EMIT errorChanged();
}
bool ShellController::launch(const QString &id) {
    auto it =
        std::find_if(apps_.begin(), apps_.end(), [&id](const App &app) { return app.id == id; });
    if (it == apps_.end()) {
        report("This application is no longer available.");
        return false;
    }
    if (!it->command.empty()) {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.remove("QT_WAYLAND_SHELL_INTEGRATION");
        process.setProcessEnvironment(env);
        process.setWorkingDirectory(QDir::homePath());
        process.setProgram(QString::fromStdString(it->command.front()));
        QStringList arguments;
        for (size_t i = 1; i < it->command.size(); ++i)
            arguments << QString::fromStdString(it->command[i]);
        process.setArguments(arguments);
        if (!process.startDetached()) {
            report("Could not launch " + it->name + ": " + process.errorString());
            return false;
        }
    } else {
        GAppLaunchContext *context = g_app_launch_context_new();
        g_app_launch_context_unsetenv(context, "QT_WAYLAND_SHELL_INTEGRATION");
        GError *error = nullptr;
        bool success = g_app_info_launch(it->info, nullptr, context, &error);
        g_object_unref(context);
        if (!success) {
            report("Could not launch " + it->name + ": " +
                   QString::fromUtf8(error ? error->message : "unknown error"));
            if (error)
                g_error_free(error);
            return false;
        }
    }
    clearError();
    return true;
}
void ShellController::reload() {
    try {
        loadConfig();
        notifications_.configure(config_.notifications);
        osd_.configure(config_.osd);
        power_.setCountdown(config_.power.countdown);
        updateNotificationService();
        refreshApps();
        Q_EMIT configChanged();
        Q_EMIT wallpaperChanged();
        if (!enabled())
            Q_EMIT disabled();
    } catch (const std::exception &error) {
        std::cerr << "Shell reload rejected: " << error.what() << '\n';
        report("Configuration unchanged: " + QString::fromUtf8(error.what()));
    }
}
void ShellController::loadConfig() {
    std::string error;
    auto next = shaodesk::load_config_or_default(path_, error);
    config_ = std::move(next);
    configError_ = QString::fromStdString(error);
    if (!error.empty())
        std::cerr << "Shell configuration error; using the default configuration: " << error
                  << '\n';
}
void ShellController::subscribe() {
    const auto path = qEnvironmentVariable("SHAODESK_SOCKET");
    if (path.isEmpty())
        return;
    state_ = new QLocalSocket(this);
    connect(state_, &QLocalSocket::connected, this, [this] { state_->write("subscribe\n"); });
    connect(state_, &QLocalSocket::readyRead, this, [this] {
        bool outputs = false, urgentSeen = false;
        while (state_->canReadLine()) {
            const auto raw = QString::fromUtf8(state_->readLine());
            const auto line = raw.trimmed();
            if (line == "ok")
                subscribed_ = true;
            else if (line.startsWith("tiling ")) {
                // Each state starts with this line and lists every output after it.
                tiling_ = line == "tiling on";
                nextWorkspaces_.clear();
                nextUrgentCount_ = 0;
                nextUrgentWindows_.clear();
                urgentSeen = true;
                outputs = true;
            } else if (line.startsWith("output ")) {
                outputs = true;
                // output NAME CURRENT OCCUPIED TILING, where OCCUPIED is "1,3" or "-" and
                // TILING is "on" or "off".
                const auto words = line.split(' ');
                if (words.size() != 5)
                    continue;
                QVariantList occupied;
                for (const auto &number : words[3].split(',', Qt::SkipEmptyParts))
                    if (number != "-")
                        occupied.push_back(number.toInt());
                nextWorkspaces_[words[1]] = QVariantMap{{"current", words[2].toInt()},
                                                        {"occupied", occupied},
                                                        {"urgent", QVariantList()},
                                                        {"tiling", words[4] == "on"}};
                continue;
            } else if (line.startsWith("urgent ")) {
                nextUrgentCount_ = line.sliced(7).toInt();
                continue;
            } else if (line.startsWith("urgent-output ")) {
                // urgent-output NAME 2,3: the workspaces of that output with urgent windows.
                const auto words = line.split(' ');
                if (words.size() != 3 || !nextWorkspaces_.contains(words[1]))
                    continue;
                QVariantList urgent;
                for (const auto &number : words[2].split(',', Qt::SkipEmptyParts))
                    urgent.push_back(number.toInt());
                auto state = nextWorkspaces_[words[1]].toMap();
                state["urgent"] = urgent;
                nextWorkspaces_[words[1]] = state;
                continue;
            } else if (line.startsWith("urgent-window ")) {
                // OUTPUT, WORKSPACE, APP_ID, TITLE, separated by tabs; read untrimmed so an
                // empty app id keeps its place.
                const auto fields = raw.sliced(14).chopped(1).split('\t');
                if (fields.size() == 4) {
                    nextUrgentWindows_.push_back(QVariantMap{{"output", fields[0]},
                                                             {"workspace", fields[1].toInt()},
                                                             {"appId", fields[2]},
                                                             {"title", fields[3]}});
                }
                continue;
            } else if (line.startsWith("focused ")) {
                const auto name = line.sliced(8) == "-" ? QString() : line.sliced(8);
                if (name != focusedOutput_) {
                    focusedOutput_ = name;
                    Q_EMIT focusedOutputChanged();
                }
                continue;
            } else if (line.startsWith("keyboard-layout ")) {
                // keyboard-layout N COUNT SHORT NAME
                const auto words = line.split(' ');
                if (words.size() < 4)
                    continue;
                const QVariantMap layout{{"number", words[1].toInt()},
                                         {"count", words[2].toInt()},
                                         {"short", words[3]},
                                         {"name", QStringList(words.mid(4)).join(' ')}};
                if (layout != keyboardLayout_) {
                    keyboardLayout_ = layout;
                    Q_EMIT keyboardLayoutChanged();
                }
                continue;
            } else if (line.startsWith("dnd ")) {
                handleDnd(line.sliced(4));
                continue;
            } else if (line.startsWith("osd ")) {
                // osd OUTPUT PERCENT TEXT
                const auto words = line.split(' ');
                if (words.size() >= 4) {
                    bool ok = false;
                    const int percent = words[2].toInt(&ok);
                    if (ok)
                        osd_.show(words[1] == "-" ? overlayOutput() : words[1],
                                  QStringList(words.mid(3)).join(' '), percent);
                }
                continue;
            } else if (line.startsWith("notifications ")) {
                Q_EMIT notificationsRequested(line.sliced(14));
                continue;
            } else if (line.startsWith("power ")) {
                // power ACTIONS: those that may run, as "lock,suspend,logout", or "-".
                power_.setAvailable(line.sliced(6));
                continue;
            } else if (line.startsWith("power-menu ")) {
                Q_EMIT powerMenuRequested(line.sliced(11));
                continue;
            } else if (line.startsWith("power-error ")) {
                report(line.sliced(12));
                continue;
            } else if (line.startsWith("launcher ")) {
                Q_EMIT launcherRequested(line.sliced(9));
                continue;
            } else if (line.startsWith("palette ")) {
                palette_.open(line.sliced(8));
                continue;
            } else if (line.startsWith("switcher ")) {
                // switcher OUTPUT SELECTED COUNT, then COUNT switcher-window lines.
                const auto words = line.split(' ');
                if (words.size() != 4)
                    continue;
                nextSwitcherOutput_ = words[1];
                nextSwitcherSelected_ = words[2].toInt();
                switcherPending_ = words[3].toInt();
                nextSwitcherWindows_.clear();
                if (switcherPending_ <= 0)
                    showSwitcher();
                continue;
            } else if (line.startsWith("switcher-window ") && switcherPending_ > 0) {
                // APP_ID, TITLE, OUTPUT, WORKSPACE, MINIMIZED, URGENT, separated by tabs. The
                // line is read untrimmed so an empty app id keeps its place.
                const auto fields = raw.sliced(16).chopped(1).split('\t');
                if (fields.size() == 5 || fields.size() == 6)
                    nextSwitcherWindows_.push_back(QVariantMap{{"appId", fields[0]},
                                                               {"title", fields[1]},
                                                               {"output", fields[2]},
                                                               {"workspace", fields[3].toInt()},
                                                               {"minimized", fields[4] == "1"},
                                                               {"urgent", fields.size() == 6 && fields[5] == "1"}});
                if (--switcherPending_ == 0)
                    showSwitcher();
                continue;
            } else if (line.startsWith("switcher-select ")) {
                switcherSelected_ = line.sliced(16).toInt();
                Q_EMIT switcherSelectedChanged();
                continue;
            } else if (line.startsWith("overview ")) {
                // overview OUTPUT COUNT SELECTED VIEWED STRIP X Y WIDTH HEIGHT FILTER (the area the
                // panels leave; the filter is "-" when empty), then COUNT overview-window and
                // STRIP overview-strip lines.
                const auto words = line.split(' ');
                if (words.size() < 11)
                    continue;
                nextOverviewArea_ = QRect(words[6].toInt(), words[7].toInt(), words[8].toInt(), words[9].toInt());
                nextOverviewOutput_ = words[1];
                nextOverviewSelected_ = words[3].toInt();
                nextOverviewViewed_ = words[4].toInt();
                nextOverviewFilter_ = words[10] == "-" && words.size() == 11 ? QString() : QStringList(words.mid(10)).join(' ');
                overviewPending_ = words[2].toInt() + words[5].toInt();
                nextOverviewWindows_.clear();
                nextOverviewStrip_.clear();
                if (overviewPending_ <= 0)
                    showOverview();
                continue;
            } else if ((line.startsWith("overview-window ") || line.startsWith("overview-strip ")) &&
                       overviewPending_ > 0) {
                // X Y WIDTH HEIGHT, then APP_ID, TITLE, WORKSPACE, URGENT (a window) or WORKSPACE,
                // WINDOWS (a strip cell) separated by tabs; read untrimmed so an empty app id stays.
                const bool window = line.startsWith("overview-window ");
                const auto rest = raw.sliced(window ? 16 : 15).chopped(1);
                const auto fields = rest.section(' ', 4).split('\t');
                QVariantMap cell{{"x", rest.section(' ', 0, 0).toInt()},
                                 {"y", rest.section(' ', 1, 1).toInt()},
                                 {"w", rest.section(' ', 2, 2).toInt()},
                                 {"h", rest.section(' ', 3, 3).toInt()}};
                if (window && (fields.size() == 3 || fields.size() == 4)) {
                    cell.insert("appId", fields[0]);
                    cell.insert("title", fields[1]);
                    cell.insert("workspace", fields[2].toInt());
                    cell.insert("urgent", fields.size() == 4 && fields[3] == "1");
                    nextOverviewWindows_.push_back(cell);
                } else if (!window && fields.size() == 2) {
                    cell.insert("workspace", fields[0].toInt());
                    cell.insert("windows", fields[1].toInt());
                    nextOverviewStrip_.push_back(cell);
                }
                if (--overviewPending_ == 0)
                    showOverview();
                continue;
            } else if (line.startsWith("overview-select ")) {
                overviewSelected_ = line.sliced(16).toInt();
                Q_EMIT overviewSelectedChanged();
                continue;
            } else if (line == "overview-close") {
                clearOverview();
                continue;
            } else if (line == "switcher-close") {
                switcherPending_ = 0;
                switcherOutput_.clear();
                switcherWindows_.clear();
                Q_EMIT switcherChanged();
                continue;
            } else
                continue;
            Q_EMIT tilingChanged();
        }
        if (outputs && workspaces_ != nextWorkspaces_) {
            workspaces_ = nextWorkspaces_;
            Q_EMIT workspacesChanged();
        }
        if (urgentSeen && (urgentCount_ != nextUrgentCount_ || urgentWindows_ != nextUrgentWindows_)) {
            urgentCount_ = nextUrgentCount_;
            urgentWindows_ = nextUrgentWindows_;
            QList<QPair<QString, QString>> windows;
            for (const auto &item : urgentWindows_)
                windows.push_back({item.toMap()["appId"].toString(), item.toMap()["title"].toString()});
            tasks_.setUrgent(windows);
            Q_EMIT urgentChanged();
        }
    });
    connect(state_, &QLocalSocket::disconnected, this, [this] {
        subscribed_ = false;
        if (!keyboardLayout_.isEmpty()) {
            keyboardLayout_.clear();
            Q_EMIT keyboardLayoutChanged();
        }
        power_.setAvailable("-");
        if (urgentCount_ != 0 || !urgentWindows_.isEmpty()) {
            urgentCount_ = 0;
            urgentWindows_.clear();
            tasks_.setUrgent({});
            Q_EMIT urgentChanged();
        }
        Q_EMIT tilingChanged();
        if (!switcherOutput_.isEmpty()) {
            switcherOutput_.clear();
            switcherWindows_.clear();
            Q_EMIT switcherChanged();
        }
        clearOverview();
    });
    state_->connectToServer(path);
}
QString ShellController::overlayOutput() const {
    const auto screens = QGuiApplication::screens();
    for (auto *screen : screens)
        if (screen->name() == focusedOutput_)
            return focusedOutput_;
    auto *primary = QGuiApplication::primaryScreen();
    return primary ? primary->name() : QString();
}
// The cards stay on one output while any is showing, so they do not jump about as the focus
// moves; the next batch goes where the focus is then.
void ShellController::updateCards() {
    // An output that has gone since the cards opened there is not one to keep them on.
    auto present = [](const QString &name) {
        const auto screens = QGuiApplication::screens();
        return std::any_of(screens.begin(), screens.end(),
                           [&name](const QScreen *screen) { return screen->name() == name; });
    };
    const QString next = notifications_.cards()->count() > 0
                             ? (cardsOutput_.isEmpty() || !present(cardsOutput_) ? overlayOutput()
                                                                                  : cardsOutput_)
                             : QString();
    if (next == cardsOutput_)
        return;
    cardsOutput_ = next;
    Q_EMIT cardsOutputChanged();
}
void ShellController::handleDnd(const QString &verb) {
    if (verb == "toggle")
        notifications_.toggleDnd();
    else if (verb == "on" || verb == "off")
        notifications_.setDnd(verb == "on");
    else
        return;
    const bool on = notifications_.dnd();
    osd_.show(overlayOutput(), on ? "Do not disturb" : "Notifications on", -1, on ? "dnd" : "notifications");
}
// The volume changed on the default output (by keys, the panel, or another program).
void ShellController::showVolume() {
    Audio *audio = audio_.get();
    if (!audio->available()) {
        lastVolume_ = -1;
        return;
    }
    const int volume = audio->volume();
    const bool muted = audio->muted();
    const QString output = audio->output();
    const bool first = lastVolume_ < 0 || output != lastVolumeOutput_;
    const bool differs = volume != lastVolume_ || muted != lastMuted_;
    lastVolume_ = volume;
    lastMuted_ = muted;
    lastVolumeOutput_ = output;
    if (first || !differs || !config_.osd.volume)
        return;
    osd_.show(overlayOutput(), muted ? "Muted" : "Volume", muted ? 0 : volume,
              muted ? "muted" : "volume");
}
bool ShellController::startNotifications() {
    serveNotifications_ = true;
    updateNotificationService();
    return notificationService_ != nullptr;
}
void ShellController::updateNotificationService() {
    if (!serveNotifications_)
        return;
#if SHAODESK_DBUS
    if (config_.notifications.enabled && !notificationService_) {
        // Without an address libdbus would start a bus of its own ("autolaunch") that no other
        // program knows of; a session with no bus has no notifications to serve.
        const bool haveBus = !qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS") ||
                             QFileInfo::exists(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/bus");
        if (!haveBus) {
            if (!noBusReported_)
                std::cerr << "shaodesk notifications: no session bus (DBUS_SESSION_BUS_ADDRESS is unset)\n";
            noBusReported_ = true;
            return;
        }
        auto *service = new NotificationService(notifications_, this);
        if (service->start()) {
            notificationService_ = service;
            std::cerr << "shaodesk notifications: serving org.freedesktop.Notifications\n";
        } else {
            std::cerr << "shaodesk notifications: " << service->error().toStdString() << '\n';
            delete service;
        }
    } else if (!config_.notifications.enabled && notificationService_) {
        delete notificationService_;
        notificationService_ = nullptr;
        notifications_.setServing(false);
    }
#endif
}
bool ShellController::startTray() {
    serveTray_ = true;
    updateTrayHost();
    return trayHost_ != nullptr;
}
void ShellController::updateTrayHost() {
    if (!serveTray_)
        return;
#if SHAODESK_TRAY
    if (!trayHost_) {
        // As for notifications: without an address libdbus would start a bus nobody knows of.
        if (qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS") &&
            !QFileInfo::exists(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/bus")) {
            std::cerr << "shaodesk tray: no session bus (DBUS_SESSION_BUS_ADDRESS is unset)\n";
            serveTray_ = false;
            return;
        }
        auto *host = new TrayHost(tray_, this);
        if (host->start()) {
            trayHost_ = host;
            std::cerr << "shaodesk tray: "
                      << (host->ownsWatcher() ? "serving org.kde.StatusNotifierWatcher"
                                              : "showing the items of another StatusNotifierWatcher")
                      << '\n';
        } else {
            std::cerr << "shaodesk tray: " << host->error().toStdString() << '\n';
            delete host;
        }
    }
#endif
}
void ShellController::showSwitcher() {
    switcherOutput_ = nextSwitcherOutput_;
    switcherWindows_ = nextSwitcherWindows_;
    switcherSelected_ = nextSwitcherSelected_;
    Q_EMIT switcherChanged();
    Q_EMIT switcherSelectedChanged();
}
void ShellController::showOverview() {
    overviewOutput_ = nextOverviewOutput_;
    overviewWindows_ = nextOverviewWindows_;
    overviewStrip_ = nextOverviewStrip_;
    overviewSelected_ = nextOverviewSelected_;
    overviewViewed_ = nextOverviewViewed_;
    overviewFilter_ = nextOverviewFilter_;
    overviewArea_ = nextOverviewArea_;
    Q_EMIT overviewChanged();
    Q_EMIT overviewSelectedChanged();
}
void ShellController::clearOverview() {
    overviewPending_ = 0;
    if (overviewOutput_.isEmpty())
        return;
    overviewOutput_.clear();
    overviewWindows_.clear();
    overviewStrip_.clear();
    overviewFilter_.clear();
    Q_EMIT overviewChanged();
}
void ShellController::switcherPick(int index) {
    if (index >= 0 && index < switcherWindows_.size())
        request(QString("switcher_confirm %1\n").arg(index + 1).toUtf8(),
                "The window switcher needs a running shaodesk session.");
}
QString ShellController::iconFor(const QString &windowAppId) const {
    const auto id = appFor(windowAppId);
    for (const auto &app : apps_)
        if (!id.isEmpty() && app.id == id)
            return app.icon;
    // A window names its own app ID: a path there is not an icon to load from disk (only a
    // desktop entry's Icon= may be a path), and it may be a file the application wrote.
    return windowAppId.isEmpty() || windowAppId.contains('/') ? QString("application-x-executable")
                                                              : windowAppId;
}
void ShellController::toggleTiling(const QString &output) {
    request(output.isEmpty() ? QByteArray("toggle_tiling\n")
                             : QString("output %1 toggle_tiling\n").arg(output).toUtf8(),
            "Tiling needs a running shaodesk session.");
}
void ShellController::showWorkspace(const QString &output, int number) {
    if (!output.isEmpty() && number >= 1 && number <= workspaceCount())
        request(QString("output %1 workspace %2\n").arg(output).arg(number).toUtf8(),
                "Workspaces need a running shaodesk session.");
}
void ShellController::send(const QString &line) {
    request(line.toUtf8() + "\n", "The compositor needs a running shaodesk session.");
}
void ShellController::ask(const QByteArray &line, std::function<void(const QByteArray &)> done) {
    const auto path = qEnvironmentVariable("SHAODESK_SOCKET");
    if (path.isEmpty())
        return;
    auto *socket = new QLocalSocket(this);
    auto reply = std::make_shared<QByteArray>();
    connect(socket, &QLocalSocket::connected, socket, [socket, line] { socket->write(line); });
    connect(socket, &QLocalSocket::readyRead, socket,
            [socket, reply] { reply->append(socket->readAll()); });
    connect(socket, &QLocalSocket::disconnected, socket, [socket, reply, done] {
        reply->append(socket->readAll());
        done(*reply);
        socket->deleteLater();
    });
    connect(socket, &QLocalSocket::errorOccurred, socket, [socket](auto error) {
        if (error != QLocalSocket::PeerClosedError)
            socket->deleteLater();
    });
    socket->connectToServer(path);
}
void ShellController::request(const QByteArray &line, const QString &unavailable) {
    const auto path = qEnvironmentVariable("SHAODESK_SOCKET");
    if (path.isEmpty()) {
        report(unavailable);
        return;
    }
    // One request per connection; the new state arrives through the subscription.
    auto *request = new QLocalSocket(this);
    connect(request, &QLocalSocket::connected, request, [request, line] { request->write(line); });
    connect(request, &QLocalSocket::disconnected, request, &QObject::deleteLater);
    connect(request, &QLocalSocket::errorOccurred, this,
            [this, request](QLocalSocket::LocalSocketError error) {
                if (error != QLocalSocket::PeerClosedError)
                    report("Could not reach the compositor: " + request->errorString());
                request->deleteLater();
            });
    request->connectToServer(path);
}
