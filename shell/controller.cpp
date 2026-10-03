// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"
#include <QDir>
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

ShellController::ShellController(std::filesystem::path path, QObject *parent)
    : QObject(parent), path_(std::move(path)), config_(shaode::load_config(path_)), tasks_(this) {
    QFile pins(pinsPath());
    if (pins.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const auto &line : QString::fromUtf8(pins.readAll()).split('\n', Qt::SkipEmptyParts))
            if (!userPins_.contains(line.trimmed()))
                userPins_.push_back(line.trimmed());
    refreshApps();
    subscribe();
}
ShellController::~ShellController() { clearApps(); }
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
QColor ShellController::panelColor() const { return color(config_.shell.panel_color); }
QColor ShellController::textColor() const { return color(config_.shell.text_color); }
QColor ShellController::background() const {
    return QColor::fromRgbF(config_.settings.background[0], config_.settings.background[1],
                            config_.settings.background[2]);
}
QUrl ShellController::wallpaper() const {
    if (config_.shell.wallpaper.empty())
        return {};
    std::filesystem::path file(config_.shell.wallpaper);
    if (file.is_relative())
        file = std::filesystem::absolute(path_).parent_path() / file;
    return QUrl::fromLocalFile(QString::fromStdString(file.string()));
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
    return state + "/shaode/pinned";
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
        auto next = shaode::load_config(path_);
        config_ = std::move(next);
        refreshApps();
        Q_EMIT configChanged();
        if (!enabled())
            Q_EMIT disabled();
    } catch (const std::exception &error) {
        std::cerr << "Shell reload rejected: " << error.what() << '\n';
        report("Configuration unchanged: " + QString::fromUtf8(error.what()));
    }
}
void ShellController::subscribe() {
    const auto path = qEnvironmentVariable("SHAODE_SOCKET");
    if (path.isEmpty())
        return;
    state_ = new QLocalSocket(this);
    connect(state_, &QLocalSocket::connected, this, [this] { state_->write("subscribe\n"); });
    connect(state_, &QLocalSocket::readyRead, this, [this] {
        bool outputs = false;
        while (state_->canReadLine()) {
            const auto raw = QString::fromUtf8(state_->readLine());
            const auto line = raw.trimmed();
            if (line == "ok")
                subscribed_ = true;
            else if (line.startsWith("tiling ")) {
                // Each state starts with this line and lists every output after it.
                tiling_ = line == "tiling on";
                nextWorkspaces_.clear();
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
                                                        {"tiling", words[4] == "on"}};
                continue;
            } else if (line.startsWith("launcher ")) {
                Q_EMIT launcherRequested(line.sliced(9));
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
                // APP_ID, TITLE, OUTPUT, WORKSPACE, MINIMIZED, separated by tabs. The line is
                // read untrimmed so an empty app id keeps its place.
                const auto fields = raw.sliced(16).chopped(1).split('\t');
                if (fields.size() == 5)
                    nextSwitcherWindows_.push_back(QVariantMap{{"appId", fields[0]},
                                                               {"title", fields[1]},
                                                               {"output", fields[2]},
                                                               {"workspace", fields[3].toInt()},
                                                               {"minimized", fields[4] == "1"}});
                if (--switcherPending_ == 0)
                    showSwitcher();
                continue;
            } else if (line.startsWith("switcher-select ")) {
                switcherSelected_ = line.sliced(16).toInt();
                Q_EMIT switcherSelectedChanged();
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
    });
    connect(state_, &QLocalSocket::disconnected, this, [this] {
        subscribed_ = false;
        Q_EMIT tilingChanged();
        if (!switcherOutput_.isEmpty()) {
            switcherOutput_.clear();
            switcherWindows_.clear();
            Q_EMIT switcherChanged();
        }
    });
    state_->connectToServer(path);
}
void ShellController::showSwitcher() {
    switcherOutput_ = nextSwitcherOutput_;
    switcherWindows_ = nextSwitcherWindows_;
    switcherSelected_ = nextSwitcherSelected_;
    Q_EMIT switcherChanged();
    Q_EMIT switcherSelectedChanged();
}
void ShellController::switcherPick(int index) {
    if (index >= 0 && index < switcherWindows_.size())
        request(QString("switcher_confirm %1\n").arg(index + 1).toUtf8(),
                "The window switcher needs a running shaoDe session.");
}
QString ShellController::iconFor(const QString &windowAppId) const {
    const auto id = appFor(windowAppId);
    for (const auto &app : apps_)
        if (!id.isEmpty() && app.id == id)
            return app.icon;
    return windowAppId.isEmpty() ? QString("application-x-executable") : windowAppId;
}
void ShellController::toggleTiling(const QString &output) {
    request(output.isEmpty() ? QByteArray("toggle_tiling\n")
                             : QString("output %1 toggle_tiling\n").arg(output).toUtf8(),
            "Tiling needs a running shaoDe session.");
}
void ShellController::showWorkspace(const QString &output, int number) {
    if (!output.isEmpty() && number >= 1 && number <= workspaceCount())
        request(QString("output %1 workspace %2\n").arg(output).arg(number).toUtf8(),
                "Workspaces need a running shaoDe session.");
}
void ShellController::request(const QByteArray &line, const QString &unavailable) {
    const auto path = qEnvironmentVariable("SHAODE_SOCKET");
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
