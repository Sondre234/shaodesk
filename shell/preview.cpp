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
#include <QQuickView>
#include <QScreen>

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
    : QObject(&controller), controller_(controller), audio_(std::make_unique<PreviewAudio>()) {
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
    if (surfaces().contains(name))
        return open(panel, "bar") && panel->window() && showSurface(panel->window()->screen(), name);
    QVariant opened;
    QMetaObject::invokeMethod(panel, "previewPopup", Q_RETURN_ARG(QVariant, opened),
                              Q_ARG(QVariant, name));
    return opened.toBool();
}

QStringList PreviewData::surfaces() {
    return {"osd-volume", "osd-text", "cards", "power-dialog", "palette", "switcher"};
}

bool PreviewData::showSurface(QScreen *screen, const QString &name) {
    // What each surface is, as the view in view.cpp that shows it on an output makes it.
    QString file;
    QVariantMap properties;
    auto mode = QQuickView::SizeViewToRootObject;
    // The offscreen platform's screen has no name, which the palette would take for no output.
    const QString output = screen->name().isEmpty() ? QStringLiteral("PREVIEW-1") : screen->name();
    if (name.startsWith("osd-")) {
        file = "Osd.qml";
        properties = {{"outputName", output}};
        // Long enough for a slow screenshot, not cut short by the display fading away.
        auto config = controller_.osd()->config();
        config.timeout = 60000;
        controller_.osd()->configure(config);
        if (name == "osd-volume")
            controller_.osd()->show(output, "Volume", 64, "volume");
        else
            controller_.osd()->show(output, "Do not disturb", -1, "dnd");
    } else if (name == "cards") {
        file = "NotificationCards.qml";
        // Over the stand-ins every preview has: one with a picture, buttons and a timer.
        QImage picture(96, 96, QImage::Format_ARGB32_Premultiplied);
        picture.fill(Qt::transparent);
        {
            QPainter painter(&picture);
            painter.setRenderHint(QPainter::Antialiasing);
            QLinearGradient gradient(0, 0, 96, 96);
            gradient.setColorAt(0, QColor("#f2a65a"));
            gradient.setColorAt(1, QColor("#b8456b"));
            painter.setBrush(gradient);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(picture.rect());
            QFont font = painter.font();
            font.setPixelSize(44);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(Qt::white);
            painter.drawText(picture.rect(), Qt::AlignCenter, "AL");
        }
        Notification message = notification("Thunderbird", "", "Ada Lovelace",
                                            "Are we still on for the review at three? I pushed "
                                            "the last fixes this morning.");
        message.desktopEntry = "thunderbird";
        message.image = picture;
        message.actions = {{"default", "Open"}, {"reply", "Reply"}, {"read", "Mark as read"}};
        message.timeout = 60000;
        controller_.notifications()->notify(message);
        // The controller puts the cards on the primary screen.
        properties = {{"outputName", controller_.cardsOutput()}};
    } else if (name == "power-dialog") {
        file = "PowerDialog.qml";
        mode = QQuickView::SizeRootObjectToView;
        controller_.power()->request("poweroff", output);
    } else if (name == "palette") {
        file = "Palette.qml";
        properties = {{"screenSize", ShellView::previewSize()}};
        controller_.palette()->open(output);
        // A search that finds applications and actions both.
        controller_.palette()->setQuery("fi");
    } else if (name == "switcher") {
        file = "Switcher.qml";
        // The taskbar's stand-in windows, most recently focused first, the one before the
        // focused one selected as Alt+Tab selects it.
        auto window = [&output](const QString &appId, const QString &title, int workspace,
                                bool minimized = false, bool urgent = false) {
            return QVariantMap{{"appId", appId},         {"title", title},
                               {"output", output},       {"workspace", workspace},
                               {"minimized", minimized}, {"urgent", urgent}};
        };
        properties = {{"screenSize", ShellView::previewSize()},
                      {"windows", QVariantList{window("firefox", "Release notes - Mozilla Firefox", 1),
                                               window("foot", "~/dev/shaodesk", 2),
                                               window("kitty", "Build finished", 2, false, true),
                                               window("foot", "htop", 2),
                                               window("org.kde.dolphin", "Downloads - Dolphin", 1, true)}},
                      {"selected", 1}};
    } else {
        return false;
    }
    surfaceName_ = name;
    surface_ = std::make_unique<QQuickView>(controller_.engine(), nullptr);
    surface_->setScreen(screen);
    surface_->setTitle("shaodesk preview " + name);
    surface_->setColor(Qt::transparent);
    surface_->setFlags(Qt::FramelessWindowHint);
    surface_->setResizeMode(mode);
    if (mode == QQuickView::SizeRootObjectToView)
        surface_->resize(ShellView::previewSize());
    surface_->setInitialProperties(properties);
    surface_->setSource(QUrl("qrc:/shell/ShaodeskShell/" + file));
    if (surface_->status() != QQuickView::Ready)
        return false;
    // As PaletteView does before it shows, and PowerView once it shows.
    if (name == "palette")
        QMetaObject::invokeMethod(surface_->rootObject(), "reset");
    surface_->show();
    if (name == "power-dialog")
        QMetaObject::invokeMethod(surface_->rootObject(), "reset");
    return true;
}

QImage PreviewData::withSurface(QImage desktop) const {
    if (!surface_ || !surface_->rootObject())
        return desktop;
    // In the desktop's pixels, which are the preview's scaled by the device pixel ratio.
    const qreal scale = desktop.width() / qreal(ShellView::previewSize().width());
    const QSizeF root = surface_->rootObject()->size();
    QImage image = surface_->grabWindow();
    image.setDevicePixelRatio(1);
    image = image.copy(0, 0, qRound(root.width() * scale), qRound(root.height() * scale));
    // Placed as its layer surface is: by the edges it is anchored to, inside the area the
    // panel leaves when it keeps clear of it (an exclusive zone of 0), over the whole output when
    // it does not (-1).
    const QRect output(QPoint(0, 0), ShellView::previewSize());
    const int panel = controller_.panelExtent();
    const QRect usable = output.adjusted(0, controller_.panelTop() ? panel : 0, 0,
                                         controller_.panelTop() ? 0 : -panel);
    const QSize size = root.toSize();
    QPoint at;
    if (surfaceName_.startsWith("osd-")) {
        // OsdView: centred, 48 pixels from the bottom edge or from the top.
        at = QPoint((output.width() - size.width()) / 2,
                    controller_.osd()->top() ? 48 : output.height() - 48 - size.height());
    } else if (surfaceName_ == "palette") {
        // PaletteView: centred, a sixth of the output's height down.
        at = QPoint(usable.left() + (usable.width() - size.width()) / 2,
                    usable.top() + output.height() / 6);
    } else if (surfaceName_ == "switcher") {
        // SwitcherView: centred.
        at = usable.center() - QPoint(size.width() / 2, size.height() / 2);
    } else if (surfaceName_ == "cards") {
        // CardsView: in the configured corner.
        const auto *center = controller_.notifications();
        at = QPoint(center->left() ? usable.left() : usable.right() + 1 - size.width(),
                    center->bottom() ? usable.bottom() + 1 - size.height() : usable.top());
    }
    QPainter painter(&desktop);
    painter.drawImage(QPointF(at) * scale, image);
    return desktop;
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
