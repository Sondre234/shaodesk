// SPDX-License-Identifier: GPL-3.0-or-later
#include "view.hpp"
#include "task_filter.hpp"
#include <QIcon>
#include <QPainter>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QScreen>
#include <iostream>
#if SHAODE_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

namespace {
class Icons : public QQuickImageProvider {
  public:
    Icons() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
        QSize dimensions = requested.isEmpty() ? QSize(48, 48) : requested;
        QIcon icon = id.startsWith('/') ? QIcon(id) : QIcon::fromTheme(id);
        if (icon.isNull())
            icon = QIcon::fromTheme("application-x-executable");
        QImage image;
        if (!icon.isNull())
            image = icon.pixmap(dimensions).toImage();
        if (image.isNull()) {
            image = QImage(dimensions, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#8aaff4"));
            painter.drawRoundedRect(image.rect().adjusted(4, 4, -4, -4), 8, 8);
            painter.setPen(QColor("#172237"));
            QFont font = painter.font();
            font.setPixelSize(dimensions.height() / 2);
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(image.rect(), Qt::AlignCenter, "+");
        }
        if (size)
            *size = image.size();
        return image;
    }
};
} // namespace
ShellView::ShellView(ShellController &controller, QScreen *screen, bool desktop, bool preview)
    : controller_(controller), desktop_(desktop), preview_(preview), outputScreen_(screen) {
    setScreen(screen);
    setTitle(desktop ? "shaoDe desktop" : "shaoDe taskbar");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setFlags(Qt::FramelessWindowHint);
    static const int registered = qmlRegisterType<TaskFilter>("ShaoDe", 1, 0, "TaskFilter");
    Q_UNUSED(registered);
    engine()->addImageProvider("icons", new Icons);
    rootContext()->setContextProperty("shell", &controller);
    rootContext()->setContextProperty("shellView", this);
    rootContext()->setContextProperty("desktopView", desktop);
    // Matches the compositor's output name, which the workspace state is keyed by.
    rootContext()->setContextProperty("outputName", screen->name());
#if SHAODE_LAYER_SHELL
    if (!preview) {
        using W = LayerShellQt::Window;
        layer_ = W::get(this);
        layer_->setScreen(screen);
        layer_->setScope(desktop ? "shaode-desktop" : "shaode-panel");
        layer_->setLayer(desktop ? W::LayerBackground : W::LayerTop);
        placeLayer();
        layer_->setKeyboardInteractivity(W::KeyboardInteractivityNone);
        layer_->setActivateOnShow(false);
    }
#endif
    resizeForContent();
    setSource(QUrl(desktop ? "qrc:/shell/Desktop.qml" : "qrc:/shell/Panel.qml"));
    connect(&controller, &ShellController::configChanged, this, [this] {
        placeLayer();
        resizeForContent();
    });
    connect(&controller, &ShellController::launcherRequested, this, [this](const QString &output) {
        if (desktop_ || !rootObject() || outputScreen_->name() != output)
            return;
        bool open = !rootObject()->property("launcherOpen").toBool();
        rootObject()->setProperty("launcherOpen", open);
        std::cerr << "shaoDe launcher " << (open ? "opened" : "closed") << " on "
                  << output.toStdString() << '\n';
    });
    connect(screen, &QScreen::geometryChanged, this, [this] { resizeForContent(); });
    connect(this, &QWindow::activeChanged, this, [this] {
        if (!isActive() && expanded_ && rootObject())
            QMetaObject::invokeMethod(rootObject(), "closeMenus");
    });
}
// The panel's surface spans the output's width and the bar's margins; the bar is drawn inset.
void ShellView::placeLayer() {
#if SHAODE_LAYER_SHELL
    if (!layer_)
        return;
    using W = LayerShellQt::Window;
    if (desktop_) {
        layer_->setAnchors(
            W::Anchors(W::AnchorLeft | W::AnchorRight | W::AnchorTop | W::AnchorBottom));
        layer_->setExclusiveZone(-1);
        return;
    }
    layer_->setAnchors(W::Anchors(W::AnchorLeft | W::AnchorRight |
                                  (controller_.panelTop() ? W::AnchorTop : W::AnchorBottom)));
    layer_->setExclusiveZone(controller_.panelExtent());
#endif
}
void ShellView::resizeForContent() {
    int width = preview_ ? 1100 : screen()->geometry().width();
    int height = desktop_ ? (preview_ ? 680 : screen()->geometry().height())
                          : (expanded_ ? std::min(560, screen()->geometry().height())
                                       : controller_.panelExtent());
    resize(width, height);
#if SHAODE_LAYER_SHELL
    if (layer_)
        layer_->setDesiredSize(QSize(0, desktop_ ? 0 : height));
#endif
}
void ShellView::setExpanded(bool expanded, bool keyboard) {
    if (desktop_)
        return;
    expanded_ = expanded;
    keyboard = expanded && keyboard;
#if SHAODE_LAYER_SHELL
    if (layer_)
        layer_->setKeyboardInteractivity(keyboard
                                             ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                             : LayerShellQt::Window::KeyboardInteractivityNone);
#endif
    resizeForContent();
    if (keyboard)
        requestActivate();
}
SwitcherView::SwitcherView(ShellController &controller, QScreen *screen)
    : controller_(controller), outputScreen_(screen), delay_(new QTimer(this)) {
    setScreen(screen);
    setTitle("shaoDe switcher");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeViewToRootObject);
    setFlags(Qt::FramelessWindowHint);
    engine()->addImageProvider("icons", new Icons);
    rootContext()->setContextProperty("shell", &controller);
    rootContext()->setContextProperty("outputName", screen->name());
    rootContext()->setContextProperty("screenSize", screen->geometry().size());
#if SHAODE_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = W::get(this);
    layer_->setScreen(screen);
    layer_->setScope("shaode-switcher");
    layer_->setLayer(W::LayerOverlay);
    layer_->setAnchors(W::Anchors());
    layer_->setExclusiveZone(0);
    layer_->setKeyboardInteractivity(W::KeyboardInteractivityNone);
    layer_->setActivateOnShow(false);
    auto fit = [this] { layer_->setDesiredSize(size()); };
    connect(this, &QWindow::widthChanged, this, fit);
    connect(this, &QWindow::heightChanged, this, fit);
#endif
    setSource(QUrl("qrc:/shell/Switcher.qml"));
    connect(screen, &QScreen::geometryChanged, this, [this] {
        rootContext()->setContextProperty("screenSize", outputScreen_->geometry().size());
    });
    // A quick Alt+Tab switches without the overlay flashing up.
    delay_->setSingleShot(true);
    delay_->setInterval(120);
    connect(delay_, &QTimer::timeout, this, [this] {
        if (controller_.switcherOutput() == outputScreen_->name()) {
            show();
            std::cerr << "shaoDe switcher shown on " << outputScreen_->name().toStdString() << '\n';
        }
    });
    connect(&controller, &ShellController::switcherChanged, this, &SwitcherView::update);
}
void SwitcherView::update() {
    if (controller_.switcherOutput() != outputScreen_->name()) {
        delay_->stop();
        if (isVisible()) {
            hide();
            std::cerr << "shaoDe switcher hidden on " << outputScreen_->name().toStdString()
                      << '\n';
        }
    } else if (!isVisible() && !delay_->isActive()) {
        delay_->start();
    }
}
