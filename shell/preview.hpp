// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QObject>
#include <QTemporaryDir>
#include <memory>

class Audio;
class QQuickItem;
class ShellController;
class SystemStatus;

// What the panel shows in `--preview-popup`, which runs without a compositor, a sound server or
// a session bus: windows (one application's two of them stacked, one minimized, one asking for
// attention), sound outputs and applications playing, a battery and Wi-Fi, tray items (one with
// a menu), a few notifications and every power action. The controller's own models take the
// tray, the notifications and the power actions; the panel's sources, which the tests swap the
// same way, take the rest.
class PreviewData : public QObject {
    Q_OBJECT
  public:
    explicit PreviewData(ShellController &controller);
    ~PreviewData() override;
    // Points the panel's sources at the stand-ins; before its first frame, so the bar is laid
    // out with them.
    void fill(QQuickItem *panel);
    // Opens popup `name` on the panel, as a click on its button would (see previewPopup in
    // Panel.qml for the names); false for a name it does not know.
    static bool open(QQuickItem *panel, const QString &name);

  private:
    QTemporaryDir sysfs_;
    std::unique_ptr<Audio> audio_;
    std::unique_ptr<SystemStatus> status_;
    QObject *tasks_ = nullptr;
};

// A screenshot of the panel's preview drawn over the desktop under it (the wallpaper, or the
// background colour), so that a translucent colour shows what it would show there.
QImage previewOnDesktop(QImage panel, const ShellController &controller);
