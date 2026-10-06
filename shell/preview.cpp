// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview.hpp"
#include "audio.hpp"
#include "controller.hpp"
#include "system_status.hpp"
#include "view.hpp"
#include <QDir>
#include <QFile>
#include <QLinearGradient>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

namespace {
// A sound server that takes every request and does nothing with it.
class PreviewAudio : public Audio {
  protected:
    void sendVolume(const QString &, int) override {}
    void sendMute(const QString &, bool) override {}
    void sendOutput(const QString &, const std::vector<uint32_t> &) override {}
    void sendStreamVolume(uint32_t, int) override {}
    void sendStreamMute(uint32_t, bool) override {}
};

// A tray icon: a rounded square in `color` with a letter on it.
QImage trayIcon(const QColor &color, const QString &letter) {
    QImage image(44, 44, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(image.rect().adjusted(2, 2, -2, -2), 10, 10);
    QFont font = painter.font();
    font.setPixelSize(26);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter, letter);
    return image;
}

TrayMenuEntry menuEntry(QVariantMap properties, std::vector<int> children = {}) {
    TrayMenuEntry entry;
    entry.properties = std::move(properties);
    entry.children = std::move(children);
    entry.read();
    return entry;
}

Notification notification(const QString &app, const QString &icon, const QString &summary,
                          const QString &body, int urgency = Notification::Normal) {
    Notification n;
    n.app = app;
    n.icon = icon;
    n.summary = summary;
    n.body = notificationMarkup(body);
    n.urgency = urgency;
    n.timeout = 0;
    return n;
}
} // namespace

PreviewData::PreviewData(ShellController &controller)
    : QObject(&controller), audio_(std::make_unique<PreviewAudio>()) {
    audio_->update({"speakers",
                    {{"speakers", "Speakers", 64, false},
                     {"headphones", "USB headphones", 40, false},
                     {"hdmi", "HDMI / DisplayPort (monitor)", 100, false}},
                    {{1, "Firefox", "firefox", 80, false},
                     {2, "Music player", "audio-x-generic", 55, false},
                     {3, "Video call", "camera-web", 100, true}}});
    // A battery that is charging and Wi-Fi that is up, in a sysfs of its own.
    auto put = [this](const QString &path, const QByteArray &text) {
        QDir(sysfs_.path()).mkpath(QFileInfo(sysfs_.filePath(path)).path());
        QFile file(sysfs_.filePath(path));
        if (file.open(QIODevice::WriteOnly))
            file.write(text);
    };
    put("class/power_supply/BAT0/type", "Battery\n");
    put("class/power_supply/BAT0/capacity", "76\n");
    put("class/power_supply/BAT0/status", "Charging\n");
    put("class/net/wlan0/device", "");
    put("class/net/wlan0/wireless", "");
    put("class/net/wlan0/operstate", "up\n");
    status_ = std::make_unique<SystemStatus>(sysfs_.path());

    TrayItem plain;
    plain.key = plain.id = plain.title = "Sync";
    plain.toolTipTitle = "Sync";
    plain.toolTipText = "Up to date";
    plain.icon = {trayIcon(QColor("#2f9e6e"), "S")};
    controller.tray()->add(plain);
    TrayItem withMenu;
    withMenu.key = withMenu.id = withMenu.title = "Updates";
    withMenu.icon = {trayIcon(QColor("#d9822b"), "U")};
    withMenu.menuPath = "/MenuBar";
    withMenu.menu = {{0, menuEntry({}, {1, 2, 3, 4, 5, 6, 7})},
                     {1, menuEntry({{"label", "_Open updates"}})},
                     {2, menuEntry({{"type", "separator"}})},
                     {3, menuEntry({{"label", "Check _automatically"},
                                    {"toggle-type", "checkmark"},
                                    {"toggle-state", 1}})},
                     {4, menuEntry({{"label", "_Schedule"}}, {41, 42})},
                     {41, menuEntry({{"label", "Daily"}, {"toggle-type", "radio"}, {"toggle-state", 1}})},
                     {42, menuEntry({{"label", "Weekly"}, {"toggle-type", "radio"}, {"toggle-state", 0}})},
                     {5, menuEntry({{"label", "Install now"}, {"enabled", false}})},
                     {6, menuEntry({{"type", "separator"}})},
                     {7, menuEntry({{"label", "_Quit"}})}};
    controller.tray()->add(withMenu);

    auto *center = controller.notifications();
    center->setServing(true);
    center->notify(notification("Updates", "system-software-update", "Three updates are ready",
                                "Restart to finish installing them."));
    center->notify(notification("Battery", "battery-caution", "Battery low",
                                "10% left: plug in the charger.", Notification::Critical));
    center->notify(notification("Calendar", "x-office-calendar", "Stand-up in 10 minutes",
                                "Room 3, or join the <b>video call</b>."));
    center->notify(notification("Mail", "mail-unread", "Re: the panel's popups",
                                "Looks good to me. Merge it once the gallery is green, and send "
                                "the screenshots around."));

    controller.power()->setAvailable("lock,suspend,hibernate,reboot,poweroff,logout");

    // The start menu as on a desktop in use: two pages of pins, applications launched lately and
    // someone logged in; those of tools/shell_gallery.py that are installed show.
    const auto now = QDateTime::currentDateTimeUtc();
    QStringList pins;
    for (const auto *id : {"firefox", "org.kde.dolphin", "foot", "thunderbird", "code", "libreoffice-writer",
                           "gimp", "spotify", "steam", "org.gnome.Calculator", "obsidian", "systemsettings",
                           "discord", "mpv", "inkscape", "org.kde.kate", "keepassxc", "obs", "blender", "krita"})
        pins << QString(id) + ".desktop";
    controller.startMenu()->preview(pins, {{"code.desktop", 14, now.addSecs(-2 * 60)},
                                           {"firefox.desktop", 40, now.addSecs(-25 * 60)},
                                           {"gimp.desktop", 3, now.addSecs(-3 * 3600)},
                                           {"thunderbird.desktop", 9, now.addDays(-1)},
                                           {"steam.desktop", 2, now.addDays(-3)},
                                           {"org.kde.kate.desktop", 5, now.addDays(-12)}});
    controller.startMenu()->setUser("Robin Lee", QUrl());

    // The windows, as the tests' stand-in model: a ListModel with the roles TaskModel has.
    QQmlComponent component(controller.engine());
    component.setData(R"(import QtQml.Models
ListModel {
    ListElement { taskId: 1; title: "Release notes - Mozilla Firefox"; appId: "firefox"; active: true; minimized: false; urgent: false }
    ListElement { taskId: 2; title: "~/dev/shaodesk"; appId: "foot"; active: false; minimized: false; urgent: false }
    ListElement { taskId: 3; title: "htop"; appId: "foot"; active: false; minimized: false; urgent: false }
    ListElement { taskId: 4; title: "Downloads - Dolphin"; appId: "org.kde.dolphin"; active: false; minimized: true; urgent: false }
    ListElement { taskId: 5; title: "Build finished"; appId: "kitty"; active: false; minimized: false; urgent: true }
})",
                      QUrl());
    tasks_ = component.create();
    if (tasks_) {
        tasks_->setParent(this);
        QQmlEngine::setObjectOwnership(tasks_, QQmlEngine::CppOwnership);
    }
}
PreviewData::~PreviewData() = default;

void PreviewData::fill(QQuickItem *panel) {
    QQmlEngine::setObjectOwnership(audio_.get(), QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(status_.get(), QQmlEngine::CppOwnership);
    panel->setProperty("audioSource", QVariant::fromValue<QObject *>(audio_.get()));
    panel->setProperty("statusSource", QVariant::fromValue<QObject *>(status_.get()));
    if (tasks_)
        panel->setProperty("taskSource", QVariant::fromValue(tasks_));
}

bool PreviewData::open(QQuickItem *panel, const QString &name) {
    QVariant opened;
    QMetaObject::invokeMethod(panel, "previewPopup", Q_RETURN_ARG(QVariant, opened),
                              Q_ARG(QVariant, name));
    return opened.toBool();
}

QImage previewOnDesktop(QImage panel, QImage popover, bool panelTop,
                        const ShellController &controller) {
    panel.setDevicePixelRatio(1);
    popover.setDevicePixelRatio(1);
    // As large as the output a preview stands for, in the bar's pixels.
    const QSize output = ShellView::previewSize() * (panel.width() / qreal(ShellView::previewSize().width()));
    QImage desktop(output, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&desktop);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QString file = controller.wallpaperFile();
    const QImage wallpaper = file.isEmpty() ? QImage() : QImage(file);
    if (!wallpaper.isNull()) {
        // Cropped to fill, as Desktop.qml shows it.
        const QSize size = wallpaper.size().scaled(desktop.size(), Qt::KeepAspectRatioByExpanding);
        painter.drawImage(QRect(QPoint((desktop.width() - size.width()) / 2,
                                       (desktop.height() - size.height()) / 2),
                                size),
                          wallpaper);
    } else {
        QLinearGradient gradient(0, 0, 0, desktop.height());
        gradient.setColorAt(0, controller.background().lighter(145));
        gradient.setColorAt(1, controller.background());
        painter.fillRect(desktop.rect(), gradient);
    }
    painter.drawImage(0, panelTop ? 0 : desktop.height() - panel.height(), panel);
    // Along the bar's edge, as the popups are placed by it, should a compositor have made the
    // window smaller.
    if (!popover.isNull())
        painter.drawImage(0, panelTop ? 0 : desktop.height() - popover.height(), popover);
    return desktop;
}
