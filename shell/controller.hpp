// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "audio.hpp"
#include "backlight.hpp"
#include "notifications.hpp"
#include "osd.hpp"
#include "palette.hpp"
#include "shaodesk/config.hpp"
#include "system_status.hpp"
#include "task_model.hpp"
#include <QColor>
#include <QLocalSocket>
#include <QMap>
#include <QObject>
#include <QRect>
#include <QUrl>
#include <QVariantList>
#include <functional>
#include <vector>

typedef struct _GAppInfo GAppInfo;
class QQmlEngine;
class ShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY configChanged)
    Q_PROPERTY(QColor panelColor READ panelColor NOTIFY configChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY configChanged)
    Q_PROPERTY(QColor background READ background NOTIFY configChanged)
    // windows.urgent_color, the colour of whatever marks a window that asks for attention.
    Q_PROPERTY(QColor urgentColor READ urgentColor NOTIFY configChanged)
    // The windows asking for attention, longest waiting first, as {output, workspace, appId,
    // title}, and how many there are. Each output's entry in `workspaces` also lists the
    // workspaces they are on, as `urgent`.
    Q_PROPERTY(int urgentCount READ urgentCount NOTIFY urgentChanged)
    Q_PROPERTY(QVariantList urgentWindows READ urgentWindows NOTIFY urgentChanged)
    // The wallpaper: one picked from the panel for the profile in use, else shell.wallpaper.
    // wallpaperFile is the same as a path, "" for none.
    Q_PROPERTY(QUrl wallpaper READ wallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(QString wallpaperFile READ wallpaperFile NOTIFY wallpaperChanged)
    // The picker's pictures as {path, name, folder} (see findWallpapers), read by
    // findWallpapers(), and the folder they come from.
    Q_PROPERTY(QVariantList wallpapers READ wallpapers NOTIFY wallpapersChanged)
    Q_PROPERTY(QString wallpaperFolder READ wallpaperFolder NOTIFY configChanged)
    Q_PROPERTY(int panelHeight READ panelHeight NOTIFY configChanged)
    Q_PROPERTY(bool panelTop READ panelTop NOTIFY configChanged)
    Q_PROPERTY(int panelMarginTop READ panelMarginTop NOTIFY configChanged)
    Q_PROPERTY(int panelMarginRight READ panelMarginRight NOTIFY configChanged)
    Q_PROPERTY(int panelMarginBottom READ panelMarginBottom NOTIFY configChanged)
    Q_PROPERTY(int panelMarginLeft READ panelMarginLeft NOTIFY configChanged)
    Q_PROPERTY(int panelExtent READ panelExtent NOTIFY configChanged)
    Q_PROPERTY(int panelRadius READ panelRadius NOTIFY configChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY configChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY configChanged)
    Q_PROPERTY(bool iconsOnly READ iconsOnly NOTIFY configChanged)
    Q_PROPERTY(bool groupWindows READ groupWindows NOTIFY configChanged)
    // Which panel widgets Lua enables: {workspaces, battery, network, volume, clock, calendar,
    // tiling, profiles}.
    Q_PROPERTY(QVariantMap widgets READ widgets NOTIFY configChanged)
    Q_PROPERTY(QVariantList pinned READ pinned NOTIFY appsChanged)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // What is wrong with the configuration while the default one stands in for it; "" when it
    // loaded.
    Q_PROPERTY(QString configError READ configError NOTIFY configChanged)
    Q_PROPERTY(TaskModel *tasks READ tasks CONSTANT)
    Q_PROPERTY(Audio *audio READ audio CONSTANT)
    Q_PROPERTY(SystemStatus *status READ status CONSTANT)
    // The notification daemon (cards, history, do-not-disturb) and the on-screen display.
    Q_PROPERTY(NotificationCenter *notifications READ notifications CONSTANT)
    Q_PROPERTY(Osd *osd READ osd CONSTANT)
    // The output the compositor says has the focus, and the one showing the notification cards
    // now: chosen when the first card appears and kept until the last is gone.
    Q_PROPERTY(QString focusedOutput READ focusedOutput NOTIFY focusedOutputChanged)
    Q_PROPERTY(QString cardsOutput READ cardsOutput NOTIFY cardsOutputChanged)
    Q_PROPERTY(bool tiling READ tiling NOTIFY tilingChanged)
    Q_PROPERTY(bool tilingAvailable READ tilingAvailable NOTIFY tilingChanged)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY configChanged)
    // layout.workspace_names, padded with "" to workspaceCount.
    Q_PROPERTY(QStringList workspaceNames READ workspaceNames NOTIFY configChanged)
    Q_PROPERTY(QVariantMap workspaces READ workspaces NOTIFY workspacesChanged)
    // The configuration's appearance profiles, sorted, and the one in use ("" for none).
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY configChanged)
    Q_PROPERTY(QString profile READ profile NOTIFY configChanged)
    // The compositor's window switcher (Alt+Tab): the output showing it, empty while closed, and
    // its windows as {appId, title, output, workspace, minimized, urgent}, most recently focused first.
    Q_PROPERTY(QString switcherOutput READ switcherOutput NOTIFY switcherChanged)
    Q_PROPERTY(QVariantList switcherWindows READ switcherWindows NOTIFY switcherChanged)
    Q_PROPERTY(int switcherSelected READ switcherSelected NOTIFY switcherSelectedChanged)
    // The command palette (Super + P).
    Q_PROPERTY(Palette *palette READ palette CONSTANT)
    // The compositor's overview: the output showing it, empty while closed; its thumbnails as
    // {x, y, w, h, appId, title, workspace, urgent} and workspace strip cells as {x, y, w, h,
    // workspace, windows}, in the output's coordinates; the selected thumbnail, the workspace
    // shown (from 1) and the typed filter. The compositor draws the thumbnails; the shell draws
    // the text over them.
    Q_PROPERTY(QString overviewOutput READ overviewOutput NOTIFY overviewChanged)
    Q_PROPERTY(QVariantList overviewWindows READ overviewWindows NOTIFY overviewChanged)
    Q_PROPERTY(QVariantList overviewStrip READ overviewStrip NOTIFY overviewChanged)
    Q_PROPERTY(int overviewViewed READ overviewViewed NOTIFY overviewChanged)
    Q_PROPERTY(QString overviewFilter READ overviewFilter NOTIFY overviewChanged)
    // What the panels leave of the output, as a rect in its coordinates.
    Q_PROPERTY(QRect overviewArea READ overviewArea NOTIFY overviewChanged)
    Q_PROPERTY(int overviewSelected READ overviewSelected NOTIFY overviewSelectedChanged)
  public:
    explicit ShellController(std::filesystem::path path, QObject *parent = nullptr);
    ~ShellController() override;
    QColor accent() const;
    QColor panelColor() const;
    QColor textColor() const;
    QColor background() const;
    QColor urgentColor() const;
    int urgentCount() const { return urgentCount_; }
    QVariantList urgentWindows() const { return urgentWindows_; }
    QUrl wallpaper() const;
    QString wallpaperFile() const;
    QVariantList wallpapers() const { return wallpapers_; }
    QString wallpaperFolder() const;
    // Reads the picker's folder again.
    Q_INVOKABLE void findWallpapers();
    // Shows `path` as the wallpaper of the profile in use, remembered across sessions until the
    // configured wallpaper changes; an empty path goes back to the configured one.
    Q_INVOKABLE void pickWallpaper(const QString &path);
    int panelHeight() const { return config_.shell.panel_height; }
    bool panelTop() const { return config_.shell.panel_top; }
    int panelMarginTop() const { return config_.shell.panel_margin[0]; }
    int panelMarginRight() const { return config_.shell.panel_margin[1]; }
    int panelMarginBottom() const { return config_.shell.panel_margin[2]; }
    int panelMarginLeft() const { return config_.shell.panel_margin[3]; }
    // The strip the panel reserves: the bar and the margins above and below it.
    int panelExtent() const { return panelHeight() + panelMarginTop() + panelMarginBottom(); }
    int panelRadius() const { return config_.shell.panel_radius; }
    QString fontFamily() const { return QString::fromStdString(config_.shell.font); }
    int fontSize() const { return config_.shell.font_size; }
    bool softwareRenderer() const { return config_.shell.software_renderer; }
    bool iconsOnly() const { return config_.shell.icons_only; }
    bool groupWindows() const { return config_.shell.group_windows; }
    bool enabled() const { return config_.shell.enabled; }
    QStringList profiles() const;
    QString profile() const { return QString::fromStdString(config_.profile); }
    // Switches to another appearance profile: the compositor saves the choice and reloads.
    Q_INVOKABLE void pickProfile(const QString &name) { send("profile " + name); }
    QVariantMap widgets() const;
    QVariantList pinned() const;
    QVariantList apps() const;
    QString error() const { return error_; }
    QString configError() const { return configError_; }
    TaskModel *tasks() { return &tasks_; }
    Audio *audio() { return audio_.get(); }
    Palette *palette() { return &palette_; }
    // Sends the compositor a request (an action, or "session restore NAME"), as `shaodesk msg`
    // would; `done` gets its whole reply. Without a session, `done` is not called.
    void ask(const QByteArray &line, std::function<void(const QByteArray &)> done);
    Q_INVOKABLE void send(const QString &line);
    // The one QML engine every view runs in, made on first use: the shell's types, icons and
    // imports load once instead of once per output.
    QQmlEngine *engine();
    SystemStatus *status() { return &status_; }
    NotificationCenter *notifications() { return &notifications_; }
    Osd *osd() { return &osd_; }
    QString focusedOutput() const { return focusedOutput_; }
    QString cardsOutput() const { return cardsOutput_; }
    // The output that overlays for the focused monitor belong on: the focused one when it
    // exists, else the primary screen.
    QString overlayOutput() const;
    // Starts answering on the session bus when notifications are enabled and QtDBus is built
    // in; later configuration reloads follow the setting. Returns whether it is serving.
    bool startNotifications();
    bool tiling() const { return tiling_; }
    bool tilingAvailable() const { return subscribed_; }
    int workspaceCount() const { return config_.settings.workspaces; }
    QStringList workspaceNames() const {
        QStringList names;
        for (int i = 0; i < workspaceCount(); ++i)
            names << (i < static_cast<int>(config_.workspace_names.size())
                          ? QString::fromStdString(config_.workspace_names[i])
                          : QString());
        return names;
    }
    // By output name: {current: N, occupied: [N, ...], tiling: bool}, numbered from 1.
    QVariantMap workspaces() const { return workspaces_; }
    QString switcherOutput() const { return switcherOutput_; }
    QVariantList switcherWindows() const { return switcherWindows_; }
    int switcherSelected() const { return switcherSelected_; }
    // Focuses the switcher's window at `index` and closes it.
    Q_INVOKABLE void switcherPick(int index);
    QString overviewOutput() const { return overviewOutput_; }
    QVariantList overviewWindows() const { return overviewWindows_; }
    QVariantList overviewStrip() const { return overviewStrip_; }
    int overviewViewed() const { return overviewViewed_; }
    QString overviewFilter() const { return overviewFilter_; }
    QRect overviewArea() const { return overviewArea_; }
    int overviewSelected() const { return overviewSelected_; }
    Q_INVOKABLE bool launch(const QString &id);
    Q_INVOKABLE void refreshApps();
    // Pins an installed application to the taskbar, remembered across sessions. Configured
    // launchers stay pinned; unpinning them means editing the configuration.
    Q_INVOKABLE void pin(const QString &id);
    Q_INVOKABLE void unpin(const QString &id);
    // Moves a pinned application to the taskbar slot of another; configured launchers keep
    // their places ahead of them.
    Q_INVOKABLE void movePin(const QString &id, const QString &target);
    // The installed application a window's app id belongs to, or empty when none matches.
    Q_INVOKABLE QString appFor(const QString &windowAppId) const;
    Q_INVOKABLE bool isPinned(const QString &id) const;
    // The pinned application whose taskbar slot a window with this app id takes over, or empty.
    Q_INVOKABLE QString pinnedAppFor(const QString &windowAppId) const;
    // The icon of a window's application, else its app id as an icon name.
    Q_INVOKABLE QString iconFor(const QString &windowAppId) const;
    Q_INVOKABLE void reload();
    Q_INVOKABLE void clearError();
    // Toggles tiling on `output`, or on the focused output when it is empty.
    Q_INVOKABLE void toggleTiling(const QString &output = {});
    Q_INVOKABLE void showWorkspace(const QString &output, int number);
  Q_SIGNALS:
    void configChanged();
    void wallpaperChanged();
    void wallpapersChanged();
    void appsChanged();
    void errorChanged();
    void disabled();
    void tilingChanged();
    void workspacesChanged();
    void urgentChanged();
    void launcherRequested(const QString &output);
    void switcherChanged();
    void switcherSelectedChanged();
    void overviewChanged();
    void overviewSelectedChanged();
    void focusedOutputChanged();
    void cardsOutputChanged();
    // The compositor asked for the notification history on `output`.
    void notificationsRequested(const QString &output);

  private:
    struct App {
        QString id, name, icon;
        shaodesk::Command command;
        GAppInfo *info = nullptr;
        bool pinned = false;
        QString wmClass;
    };
    // Loads the configuration, or the default one with configError_ set when it has an error.
    void loadConfig();
    std::filesystem::path path_;
    shaodesk::Config config_;
    TaskModel tasks_;
    Palette palette_{*this};
    std::unique_ptr<Audio> audio_ = makeAudio();
    SystemStatus status_{"/sys", nullptr, true};
    NotificationCenter notifications_;
    Osd osd_;
    // $SHAODESK_SYSFS names another sysfs tree, polled, for tests.
    Backlight backlight_{qEnvironmentVariable("SHAODESK_SYSFS", "/sys"), !qEnvironmentVariableIsSet("SHAODESK_SYSFS"),
                         qEnvironmentVariableIsSet("SHAODESK_SYSFS") ? 100 : 0};
    QString focusedOutput_, cardsOutput_;
    QObject *notificationService_ = nullptr;
    bool serveNotifications_ = false, noBusReported_ = false;
    int lastVolume_ = -1;
    bool lastMuted_ = false;
    QString lastVolumeOutput_;
    void updateCards();
    void updateNotificationService();
    void showVolume();
    void handleDnd(const QString &verb);
    std::vector<App> apps_;
    // Desktop ids pinned from the shell, in the order they were pinned.
    QStringList userPins_;
    QString error_;
    QString configError_;
    QQmlEngine *engine_ = nullptr;
    // Compositor state from its control socket ($SHAODESK_SOCKET), kept open by "subscribe".
    QLocalSocket *state_ = nullptr;
    bool subscribed_ = false, tiling_ = false;
    QVariantMap workspaces_, nextWorkspaces_;
    int urgentCount_ = 0, nextUrgentCount_ = 0;
    QVariantList urgentWindows_, nextUrgentWindows_;
    QString switcherOutput_, nextSwitcherOutput_;
    QVariantList switcherWindows_, nextSwitcherWindows_;
    int switcherSelected_ = 0, nextSwitcherSelected_ = 0, switcherPending_ = 0;
    void showSwitcher();
    QString overviewOutput_, nextOverviewOutput_, overviewFilter_, nextOverviewFilter_;
    QVariantList overviewWindows_, nextOverviewWindows_, overviewStrip_, nextOverviewStrip_;
    int overviewSelected_ = 0, nextOverviewSelected_ = 0, overviewViewed_ = 1,
        nextOverviewViewed_ = 1, overviewPending_ = 0;
    QRect overviewArea_, nextOverviewArea_;
    void showOverview();
    void clearOverview();
    void subscribe();
    void request(const QByteArray &line, const QString &unavailable);
    void report(const QString &message);
    void clearApps();
    void sortApps();
    static QString pinsPath();
    void savePins();
    // Wallpapers picked from the panel, by profile: the one picked, and shell.wallpaper as it
    // was then; the pick stands while that is still what the configuration says.
    struct PickedWallpaper {
        QString configured, picked;
    };
    QMap<QString, PickedWallpaper> pickedWallpapers_;
    QVariantList wallpapers_;
    static QString pickedWallpapersPath();
    static QVariantMap record(const App &app);
};
