// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "audio.hpp"
#include "shaode/config.hpp"
#include "task_model.hpp"
#include <QColor>
#include <QLocalSocket>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <vector>

typedef struct _GAppInfo GAppInfo;
class ShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY configChanged)
    Q_PROPERTY(QColor panelColor READ panelColor NOTIFY configChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY configChanged)
    Q_PROPERTY(QColor background READ background NOTIFY configChanged)
    Q_PROPERTY(QUrl wallpaper READ wallpaper NOTIFY configChanged)
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
    Q_PROPERTY(QVariantList pinned READ pinned NOTIFY appsChanged)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(TaskModel *tasks READ tasks CONSTANT)
    Q_PROPERTY(Audio *audio READ audio CONSTANT)
    Q_PROPERTY(bool tiling READ tiling NOTIFY tilingChanged)
    Q_PROPERTY(bool tilingAvailable READ tilingAvailable NOTIFY tilingChanged)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY configChanged)
    Q_PROPERTY(QVariantMap workspaces READ workspaces NOTIFY workspacesChanged)
    // The compositor's window switcher (Alt+Tab): the output showing it, empty while closed, and
    // its windows as {appId, title, output, workspace, minimized}, most recently focused first.
    Q_PROPERTY(QString switcherOutput READ switcherOutput NOTIFY switcherChanged)
    Q_PROPERTY(QVariantList switcherWindows READ switcherWindows NOTIFY switcherChanged)
    Q_PROPERTY(int switcherSelected READ switcherSelected NOTIFY switcherSelectedChanged)
  public:
    explicit ShellController(std::filesystem::path path, QObject *parent = nullptr);
    ~ShellController() override;
    QColor accent() const;
    QColor panelColor() const;
    QColor textColor() const;
    QColor background() const;
    QUrl wallpaper() const;
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
    bool iconsOnly() const { return config_.shell.icons_only; }
    bool groupWindows() const { return config_.shell.group_windows; }
    bool enabled() const { return config_.shell.enabled; }
    QVariantList pinned() const;
    QVariantList apps() const;
    QString error() const { return error_; }
    TaskModel *tasks() { return &tasks_; }
    Audio *audio() { return audio_.get(); }
    bool tiling() const { return tiling_; }
    bool tilingAvailable() const { return subscribed_; }
    int workspaceCount() const { return config_.settings.workspaces; }
    // By output name: {current: N, occupied: [N, ...], tiling: bool}, numbered from 1.
    QVariantMap workspaces() const { return workspaces_; }
    QString switcherOutput() const { return switcherOutput_; }
    QVariantList switcherWindows() const { return switcherWindows_; }
    int switcherSelected() const { return switcherSelected_; }
    // Focuses the switcher's window at `index` and closes it.
    Q_INVOKABLE void switcherPick(int index);
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
    void appsChanged();
    void errorChanged();
    void disabled();
    void tilingChanged();
    void workspacesChanged();
    void launcherRequested(const QString &output);
    void switcherChanged();
    void switcherSelectedChanged();

  private:
    struct App {
        QString id, name, icon;
        shaode::Command command;
        GAppInfo *info = nullptr;
        bool pinned = false;
        QString wmClass;
    };
    std::filesystem::path path_;
    shaode::Config config_;
    TaskModel tasks_;
    std::unique_ptr<Audio> audio_ = makeAudio();
    std::vector<App> apps_;
    // Desktop ids pinned from the shell, in the order they were pinned.
    QStringList userPins_;
    QString error_;
    // Compositor state from its control socket ($SHAODE_SOCKET), kept open by "subscribe".
    QLocalSocket *state_ = nullptr;
    bool subscribed_ = false, tiling_ = false;
    QVariantMap workspaces_, nextWorkspaces_;
    QString switcherOutput_, nextSwitcherOutput_;
    QVariantList switcherWindows_, nextSwitcherWindows_;
    int switcherSelected_ = 0, nextSwitcherSelected_ = 0, switcherPending_ = 0;
    void showSwitcher();
    void subscribe();
    void request(const QByteArray &line, const QString &unavailable);
    void report(const QString &message);
    void clearApps();
    void sortApps();
    static QString pinsPath();
    void savePins();
    static QVariantMap record(const App &app);
};
